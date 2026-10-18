#include "../src/assistant_context.hpp"
#include <cmath>
// D.02/A.04: current observed receipts drive recovery, never polling budgets.
static V semanticReferenceReceipt041d(unsigned revision=1){
    V result=json(R"({"isError":false,"structuredContent":{"revision":1,"id":1,"kind":"face","topology":{"references":[{"name":"z_min","kind":"face","status":"bound","descriptor":{"normal":[0,0,-1]}}]}}})");
    V data=*result.find("structuredContent");data.set("revision",V::makeNum(revision));result.set("structuredContent",data);return result;
}
static V reducedReadTools041d(){
    V tools=ark::AgentSession::tools();tools.arr.erase(std::remove_if(tools.arr.begin(),tools.arr.end(),[](const auto& tool){return tool.find("name")->asStr()=="query_batch";}),tools.arr.end());return tools;
}
static void reducedCatalogReadRecovery041d(){
    for(const std::string mode:{"find","reference","wrong_subject","stale","failed","missing_revision","other_copy","doc_info","object_context"}){
        ark::AgentSession session;session.suppressIterationHistory();check(!session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Reduced catalogue body","p":{"dx":10,"dy":8,"dz":4}}]})")).find("isError")->b,"reduced catalogue fixture starts with an actual editable body");
        ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};int round=0,writes=0;const bool selected=mode=="find"||mode=="reference",exact=mode=="doc_info"||mode=="object_context",recovered=selected||exact;V report;
        const std::string tool=exact?mode:"reference_inspect";
        V failed=mode=="doc_info"?json(R"({"detail":"summary"})"):mode=="object_context"?json(R"({"ids":[1],"expected_revision":1})"):json(R"({"id":1,"reference":"missing.synthetic.face","detail":"frame"})");
        V evidence=mode=="doc_info"?json(R"({"detail":"revision"})"):mode=="object_context"?json(R"({"ids":[2],"expected_revision":1})"):mode=="reference"?json(R"({"id":1,"reference":"z_min","detail":"frame"})"):json(R"({"id":1,"find":{"kind":"planar","normal":"-Z"},"detail":"frame"})");
        if(mode=="wrong_subject")evidence.set("id",V::makeNum(2));if(mode=="other_copy"){failed.set("copy",V::makeNum(0));evidence.set("copy",V::makeNum(1));}
        config.roles[0].transport=[&](auto&,const V& request,auto*){
            switch(++round){
            case 1:{bool offeredBatch=false;for(const auto& offered:request.find("tools")->arr)offeredBatch|=offered.find("name")->asStr()=="query_batch";check(!offeredBatch,"recovery catalogue genuinely omits query_batch");return call("reduced_rejected_read",tool,failed.dump());}
            case 2:check(team.snapshot().roles[0].unresolvedToolErrors==1,"reduced catalogue retains the original read occurrence");return call("reduced_different_read",tool,evidence.dump());
            case 3:{
                const auto receipt=lastTeamReceipt038d(request);check(team.snapshot().roles[0].unresolvedToolErrors==(mode=="failed"?2u:1u),"a different successful selection never silently clears an earlier read merely because batch discovery is absent");
                if(selected){const auto& next=*receipt.find("team_progress")->find("next_action");check(next.find("action")->str=="confirm_corrected_read"&&next.find("requires_author_decision")->b,"literal or FIND recovery receives the same explicit author decision");report=*next.find("args");const auto& resolution=report.find("read_resolutions")->arr[0];const auto& retainedArgs=*resolution.find("evidence")->find("args");check(resolution.find("failed")->find("args")->dump()==failed.dump()&&retainedArgs.obj.size()==evidence.obj.size(),"reduced catalogue proposal retains the exact rejected and successful query argument fields");for(const auto& field:evidence.obj)check(retainedArgs.find(field.first.c_str())&&retainedArgs.find(field.first.c_str())->dump()==field.second.dump(),"all observed successful query argument values are preserved without relying on object key order");if(mode=="find")check(retainedArgs.find("find")&&resolution.find("reason")->str.find("producer-authored semantic")==std::string::npos,"FIND evidence is attributed as the actual selected inspection rather than a literal semantic request");return call("reduced_author_confirmation","team_report_task",report.dump());}
                if(exact)return call("reduced_exact_recovery",tool,failed.dump());
                if(mode!="failed"&&mode!="missing_revision")check(receipt.find("team_progress")->find("next_action")->find("action")->str!="confirm_corrected_read","wrong-subject or stale FIND cannot become an author-confirmation proposal");
                return call("reduced_reject_completion","team_finish_goal",R"({"state":"completed","summary":"Attempt to ignore the original read failure."})");
            }
            case 4:
                if(recovered){check(team.snapshot().roles[0].unresolvedToolErrors==0,"only explicit corrected-selection confirmation or an exact current read resolves the retained error");return call("reduced_actual_cad_edit","doc_apply",R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dx":22}}]})");}
                check(lastTeamReceipt038d(request).find("isError")->b,"reduced catalogue cannot complete with the original unresolved read");return done("Original reduced-catalogue read remains rejected.");
            case 5:check(session.revision==2&&writes==1,"reduced catalogue recovery reaches one actual geometry publication");return call("reduced_completed","team_finish_goal",R"({"state":"completed","summary":"Recovered the exact read and authored the next editable checkpoint."})");
            default:return done("Reduced catalogue recovery checked.");
            }
        };
        team.start(config,"Recover a selected read without the optional batch envelope",reducedReadTools041d(),[&](const std::string& name,const V& args){
            if(round==1||((mode=="failed")&&round==2))return json(R"({"isError":true,"structuredContent":{"revision":1,"error_code":"EVALUATION_FAILED","error":"Original selected read rejected."}})");
            if(name=="reference_inspect"){auto receipt=semanticReferenceReceipt041d(mode=="stale"?0:1);V data=*receipt.find("structuredContent");data.obj.erase(std::remove_if(data.obj.begin(),data.obj.end(),[](const auto& field){return field.first=="topology";}),data.obj.end());data.set("reference",V::makeStr("z_min"));if(mode=="wrong_subject")data.set("id",V::makeNum(2));if(mode=="missing_revision")data.obj.erase(std::remove_if(data.obj.begin(),data.obj.end(),[](const auto& field){return field.first=="revision";}),data.obj.end());receipt.set("structuredContent",data);return receipt;}
            if(mode=="object_context"&&round==2)return json(R"({"isError":false,"structuredContent":{"revision":1,"objects":[]}})");
            if(name=="doc_apply")++writes;return session.call(name,args);
        });complete(team);const auto snapshot=team.snapshot();check(snapshot.roles[0].state==ark::AgentTeamState::Idle&&snapshot.unresolvedErrors==(recovered?0u:mode=="failed"?2u:1u)&&session.revision==(recovered?2u:1u)&&(recovered?snapshot.goalState==ark::AgentGoalState::Completed:snapshot.goalState!=ark::AgentGoalState::Completed),("reduced catalogue strict recovery: "+mode+" / "+snapshot.roles[0].error).c_str());
    }
}
static void observedReadProgress041d(){
    reducedCatalogReadRecovery041d();
    for(const std::string mode:{"predicate","frame","wrong_normal","ambiguous","split","wrong_receipt_id","unmeasured","stale","known_newer","other_target","other_copy","failed","project","failed_write"}){
        ark::AgentSession session;session.suppressIterationHistory();
        check(!session.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Synthetic recovery body","p":{"dx":10,"dy":8,"dz":4}}]})")).find("isError")->b,"synthetic editable body starts at revision one");
        if(mode=="known_newer")check(!session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"external_change","expr":2}]})")).find("isError")->b,"newer known revision is an actual external publication");
        ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
        const bool automatic=mode=="predicate",proposal=mode=="frame"||mode=="wrong_normal"||mode=="ambiguous";const unsigned retained=mode=="failed"?2:1;int round=0,writes=0;V report;
        V failedArgs=json(R"({"id":1,"find":{"kind":"planar","normal":"-Z"},"detail":"frame"})"),evidenceArgs=json(R"({"id":1,"reference":"z_min","detail":"full"})");
        if(mode=="other_target")evidenceArgs.set("id",V::makeNum(2));
        if(mode=="other_copy"){failedArgs.set("copy",V::makeNum(0));evidenceArgs.set("copy",V::makeNum(1));}
        config.roles[0].transport=[&](auto&,const V& request,auto*){
            switch(++round){
            case 1:return mode=="failed_write"?call("rejected_write","doc_apply",R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dx":"missing_input"}}]})"):call("rejected_find","reference_inspect",failedArgs.dump());
            case 2:check(team.snapshot().roles[0].unresolvedToolErrors==1,"original failed operation is retained before later evidence");return call("observed_semantic","reference_inspect",evidenceArgs.dump());
            case 3:{
                const auto snapshot=team.snapshot(true,0,true);const auto receipt=lastTeamReceipt038d(request);check(snapshot.roles[0].unresolvedToolErrors==(automatic?0u:retained),"only an actually verified native predicate is automatically superseded");
                const auto* progress=receipt.find("team_progress");
                if(mode!="failed"&&mode!="unmeasured")check(progress&&progress->find("revision")&&progress->find("evidence_scope")->str=="team_coordination_only; actual_read_result_preserved_and_not_compared"&&progress->find("coordination_fingerprint")->str.size()==64,"measured successful inspection labels bounded coordination evidence separately from actual read geometry");
                if(mode=="wrong_receipt_id")check(progress->find("next_action")->find("action")->str!="confirm_corrected_read","contradictory echoed target ID cannot become a validated author proposal");
                if(automatic){const auto& resolved=progress->find("read_resolutions")->arr.at(0);check(resolved.find("selection_predicate_verified")->b&&!resolved.find("original_query_succeeded")->b&&resolved.find("role")->str=="engineer"&&resolved.find("revision")->num==1&&resolved.find("evidence_read_sequence")->num>resolved.find("failed_read_sequence")->num&&resolved.find("project_epoch"),"automatic supersession retains exact actor, revision, occurrence sequence and project custody without rewriting original failure");return call("actual_geometry_edit","doc_apply",R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dx":22}}]})");}
                if(proposal){const auto& next=*progress->find("next_action");check(next.find("action")->str=="confirm_corrected_read"&&next.find("requires_author_decision")->b&&next.find("tool")->str=="team_report_task","frame or different-face evidence needs an explicit author intent decision");report=*next.find("args");check(report.find("read_resolutions")->arr[0].find("failed")->find("args")->dump()==failedArgs.dump(),"ready action preserves exact failed selector args");return call("selected_context","object_context",R"({"ids":[1],"expected_revision":1})");}
                if(mode=="stale"||mode=="known_newer")check(progress->find("next_action")->find("action")->str=="inspect_current_revision","older proof requests current inspection instead of preparing a stale write");
                return call("reject_hidden_error","team_finish_goal",R"({"state":"completed","summary":"Attempt to bypass a retained error."})");
            }
            case 4:
                if(automatic){check(session.revision==2&&lastTeamReceipt038d(request).find("structuredContent")->find("revision")->num==2,"automatic recovery is followed by a real atomic geometry write");return call("finish_auto","team_finish_goal",R"({"state":"completed","summary":"Verified original selector predicate and authored the next editable body checkpoint."})");}
                if(proposal){check(lastTeamReceipt038d(request).find("team_progress")->find("next_action")->find("args")->dump()==report.dump(),"selected inventory does not lose the exact retained recovery proposal");return call("inventory_revision","doc_info",R"({"detail":"revision"})");}
                check(lastTeamReceipt038d(request).find("isError")->b,"unrelated, failed, stale or noncurrent evidence cannot hide an unresolved operation");return call("remaining_input","team_report_task",R"({"state":"needs_input","summary":"The original selector or failed write still needs its own correction."})");
            case 5:
                if(!proposal)return done("Observed recovery scope checked.");
                check(lastTeamReceipt038d(request).find("team_progress")->find("next_action")->find("args")->dump()==report.dump(),"alternating document inventory preserves same concrete recovery action");return call("selected_context_again","object_context",R"({"ids":[1],"expected_revision":1})");
            case 6:
                check(lastTeamReceipt038d(request).find("team_progress")->find("evidence_state")->str=="no_new_evidence","same query at unchanged revision/task does not manufacture progress");return call("author_confirms_intent","team_report_task",report.dump());
            case 7:check(!lastTeamReceipt038d(request).find("isError")&&team.snapshot().roles[0].unresolvedToolErrors==0,"host revalidates exact author-confirmed correction at the actual current revision");return call("actual_geometry_after_report","doc_apply",R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"p":{"dx":22}}]})");
            case 8:check(session.revision==2,"author-confirmed recovery leads to an actual atomic CAD edit");return call("finish_confirmed","team_finish_goal",R"({"state":"completed","summary":"Confirmed intended semantic replacement and authored the next editable body checkpoint."})");
            default:return done("Current read recovery checked.");
            }
        };
        team.start(config,"Recover the intended synthetic face and continue editable CAD work",ark::AgentSession::tools(),[&](const std::string& tool,const V& args){
            if(tool=="reference_inspect"){
                if(args.find("find")||mode=="failed"){auto failure=json(R"({"isError":true,"structuredContent":{"revision":1,"error_code":"EVALUATION_FAILED","error":"No reference matched"}})");if(mode=="known_newer"){auto data=*failure.find("structuredContent");data.set("revision",V::makeNum(2));failure.set("structuredContent",data);}return failure;}
                V receipt=semanticReferenceReceipt041d(mode=="stale"?0:1),data=*receipt.find("structuredContent");
                if(mode=="frame")data.obj.erase(std::remove_if(data.obj.begin(),data.obj.end(),[](const auto& entry){return entry.first=="topology";}),data.obj.end());
                if(mode=="wrong_receipt_id")data.set("id",V::makeNum(2));
                if(mode=="other_target")data.set("id",V::makeNum(2));
                if(mode=="unmeasured")data.obj.erase(std::remove_if(data.obj.begin(),data.obj.end(),[](const auto& entry){return entry.first=="revision";}),data.obj.end());
                if(mode=="project"){data.set("activity_project_id",V::makeStr("new-project"));data.set("activity_boundary",V::makeBool(true));}
                if(mode=="wrong_normal"||mode=="ambiguous"||mode=="split"){
                    V topology=*data.find("topology"),rows=*topology.find("references");
                    if(mode=="wrong_normal")rows.arr[0].set("descriptor",json(R"({"normal":[0,0,1]})"));
                    if(mode=="ambiguous"){V other=rows.arr[0];other.set("name",V::makeStr("other_downward_face"));rows.arr.push_back(other);}
                    if(mode=="split")rows.arr[0].set("status",V::makeStr("split"));
                    topology.set("references",rows);data.set("topology",topology);
                }
                receipt.set("structuredContent",data);return receipt;
            }
            if(tool=="doc_apply")++writes;return session.call(tool,args);
        });complete(team);
        const auto snapshot=team.snapshot(true,0,true);check(snapshot.roles[0].state==ark::AgentTeamState::Idle,("scripted read progress state: "+mode+" / "+snapshot.roles[0].error).c_str());
        const bool recovered=automatic||proposal;check(session.revision==(recovered||mode=="known_newer"?2u:1u)&&snapshot.unresolvedErrors==(recovered?0u:retained)&&(recovered?snapshot.goalState==ark::AgentGoalState::Completed:snapshot.goalState!=ark::AgentGoalState::Completed),"recovery permits only the owned current operation, while all negatives retain the original blocker");
        if(recovered)check(writes==1&&session.document.features[0].p.at("dx")=="22","successful recovery publishes actual edited dimensions once");
    }
}
static void inspectionActionPriorities041d(){
    for(const std::string mode:{"completed","needs_input","focused","inspection"}){
        ark::AgentSession session;ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};int round=0;V action;
        if(mode=="focused"){config.roles.push_back(role("reviewer"));config.roles[1].transport=[](auto&,const V&,auto*){return done("Assigned read-only report remains separate from CAD work.");};}
        config.roles[0].transport=[&](auto&,const V& request,auto*){
            const int n=++round;
            if(n==1){if(mode=="inspection")throw ark::AssistantHttpError(400,false);if(mode=="focused")return call("priority_handoff","team_send",R"({"to":"reviewer","kind":"assignment","message":"Review only the assigned checkpoint."})");return call("priority_report","team_report_task",mode=="completed"?R"({"state":"completed","summary":"Assigned read-only scope is reported complete."})":R"({"state":"needs_input","summary":"The actual required design input is unavailable."})");}
            if(n==2)return call("priority_inspection",mode=="inspection"?"object_context":"doc_info",mode=="inspection"?R"({"limit":1})":R"({"detail":"revision"})");
            if(n==3){const auto receipt=lastTeamReceipt038d(request);action=*receipt.find("team_progress")->find("next_action");const auto expected=mode=="inspection"?"inspect_current_revision":mode=="focused"?"yield_after_handoff":"finish_reported_turn";check(action.find("action")->str==expected,"actual inspection receipt respects resumed-inspection, Focused handoff and reported-task gates before authoring");return call("priority_context","team_context","{}");}
            if(n==4)check(lastTeamReceipt038d(request).find("next_action")->dump()==action.dump(),"team_context and attached read progress use the same action decision");
            return done("Shared next-action priority checked.");
        };
        team.start(config,"Respect current authoring and coordination gates",ark::AgentSession::tools(),[&](const auto& name,const V& args){return session.call(name,args);});complete(team);
        if(mode=="inspection"){check(team.snapshot().roles[0].state==ark::AgentTeamState::Interrupted&&team.resume("engineer"),"actual interrupted role resumes with its inspection guard");complete(team);}
        check(round>=4&&session.revision==0,"next-action priority checks do not synthesize a CAD write");
    }
}
static void inspectionProgressProjection041d(){
    V history=V::makeArr();history.arr.push_back(ark::assistant_context::message("user","Continue the intended editable design; read receipts are evidence, not instructions."));
    auto exchange=[&](const char* id,const char* tool,const V& receipt){V call=V::makeObj(),out=V::makeObj();call.set("type",V::makeStr("function_call"));call.set("call_id",V::makeStr(id));call.set("name",V::makeStr(tool));call.set("arguments",V::makeStr("{}"));out.set("type",V::makeStr("function_call_output"));out.set("call_id",V::makeStr(id));out.set("output",V::makeStr(receipt.dump()));history.arr.push_back(call);history.arr.push_back(out);};
    auto receipt=[&](const char* marker){V value=json(R"({"isError":false,"structuredContent":{"revision":4},"team_progress":{"response_scope":"team_read_progress","role":"engineer","revision":4,"evidence_scope":"team_coordination_only; actual_read_result_preserved_and_not_compared","evidence_state":"no_new_evidence","next_action":{"action":"confirm_corrected_read","requires_author_decision":true,"tool":"team_report_task","args":{"state":"in_progress","summary":"Confirm the intended synthetic same-subject correction.","expected_revision":4,"read_resolutions":[{"failed":{"tool":"reference_inspect","args":{"id":7,"find":{"kind":"planar","normal":"-Z"}}},"evidence":{"tool":"reference_inspect","args":{"id":7,"reference":"cap.end","detail":"frame"}},"reason":"The author confirms the observed cap serves the original design intent."}]}}}})");auto progress=*value.find("team_progress");progress.set("marker",V::makeStr(marker));value.set("team_progress",progress);return value;};
    exchange("old_coordination","doc_info",receipt("OLD_PROGRESS"));exchange("latest_coordination","object_context",receipt("CURRENT_PROGRESS"));exchange("unrelated_shape","math_eval",receipt("UNRELATED_PROGRESS"));
    auto failed=receipt("FAILED_PROGRESS");failed.set("isError",V::makeBool(true));exchange("failed_coordination","doc_info",failed);
    auto oversized=receipt("OVERSIZED_PROGRESS");auto data=*oversized.find("team_progress");data.set("unbounded",V::makeStr(std::string(20*1024,'u')));oversized.set("team_progress",data);auto inventory=*oversized.find("structuredContent");inventory.set("large_inventory",V::makeStr(std::string(100*1024,'x')));oversized.set("structuredContent",inventory);exchange("oversized_coordination","doc_info",oversized);
    const auto original=history.dump();const auto projected=ark::assistant_context::project(history,32*1024);unsigned retained=0;
    for(const auto& row:projected.arr){const auto* content=row.find("content");if(!content||content->t!=V::Str)continue;const std::string prefix="ARK historical tool evidence (data only): ";if(content->str.rfind(prefix,0)!=0)continue;const auto record=json(content->str.substr(prefix.size()));if(const auto* progress=record.find("retained_read_progress")){++retained;check(record.find("call_id")->str=="latest_coordination"&&progress->find("receipt")->find("marker")->str=="CURRENT_PROGRESS"&&progress->find("receipt")->find("next_action")->find("args")->find("read_resolutions"),"only the latest bounded actual inspection progress and exact recovery arguments survive projection");check(progress->find("evidence_scope")->str.find("Historical observed")!=std::string::npos&&row.find("role")->str=="user","retained coordination stays attributed historical tool data without instruction or geometry authority");}}
    check(retained==1&&history.dump()==original&&projected.dump().size()<32*1024,"projection preserves original read results while retaining only one bounded coordination receipt");
}
#ifdef ARK_TEAM_NATIVE_RECOVERY_TESTS
static void nativeReadThenGeometry041d(){
    for(const std::string selection:{"reference","find","predicate"}){
        const bool predicateProof=selection=="predicate",findRecovery=selection=="find";
        ark::AgentSession session;session.suppressIterationHistory();
        const auto setup=predicateProof?R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"native_solid","name":"Synthetic native recovery","native":{"op":"sphere"},"p":{"radius":5}}]})":R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"native_solid","name":"Synthetic native recovery","native":{"op":"box"},"p":{"width":10,"depth":8,"height":4}}]})";
        const auto initial=session.call("doc_apply",json(setup));check(!initial.find("isError")->b,"synthetic native body is authored through the actual OCCT Session");
        ark::AgentTeam team;ark::AgentTeamConfig config;config.focusedWorkflow=true;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;int round=0,writes=0;V report;
        const auto baseline=predicateProof?2u:1u;
        config.roles[0].transport=[&](auto&,const V& request,auto*){
            switch(++round){
            case 1:return call("native_original_failure","reference_inspect",predicateProof?R"({"id":1,"find":{"kind":"planar","normal":"-Z"}})":R"({"id":1,"reference":"missing.synthetic.face","detail":"frame"})");
            case 2:{
                check(lastTeamReceipt038d(request).find("isError")->b&&team.snapshot().roles[0].unresolvedToolErrors==1,"actual original native selector failure remains visible");
                if(predicateProof){const auto changed=session.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"feature.edit","id":1,"native":{"op":"box"},"p":{"width":10,"depth":8,"height":4}}]})"));check(!changed.find("isError")->b&&session.revision==2,"an explicit source edit makes the original unique planar predicate true at a later revision");}
                return call("native_corrected_semantic","reference_inspect",predicateProof?R"({"id":1,"reference":"z_min","detail":"full"})":findRecovery?R"({"id":1,"find":{"kind":"planar","normal":"-Z"},"detail":"frame"})":R"({"id":1,"reference":"z_min","detail":"frame"})");
            }
            case 3:{
                const auto receipt=lastTeamReceipt038d(request);check(!receipt.find("isError")->b&&receipt.find("structuredContent")->find("world_column_major"),"corrected reference resolves an actual current native frame");
                const auto& progress=*receipt.find("team_progress");
                if(predicateProof){check(team.snapshot().roles[0].unresolvedToolErrors==0&&progress.find("read_resolutions")->arr[0].find("selection_predicate_verified")->b,"actual full B-rep topology proves the original unique -Z selector before automatic recovery");}
                else{const auto& next=*progress.find("next_action");check(next.find("requires_author_decision")->b&&team.snapshot().roles[0].unresolvedToolErrors==1,"actual compact frame supplies a ready author decision and retains the original rejected reference");report=*next.find("args");if(findRecovery)check(report.find("read_resolutions")->arr[0].find("evidence")->find("args")->find("find")&&!report.find("read_resolutions")->arr[0].find("evidence")->find("args")->find("reference"),"actual native FIND receipt supplies its exact selected inspection without inventing a literal semantic request");return call("native_author_decision","team_report_task",report.dump());}
                break;
            }
            case 4:
                if(!predicateProof){check(!lastTeamReceipt038d(request).find("isError")&&team.snapshot().roles[0].unresolvedToolErrors==0,"native author-confirmed correction is reverified at the actual current revision");break;}
                check(session.revision==baseline+1&&team.snapshot().roles[0].stepsApplied==1,"recovery reaches a real new native geometry checkpoint");return call("native_finish_automatic","team_finish_goal",R"({"state":"completed","summary":"Verified the native selection predicate and published the next checked editable body."})");
            case 5:
                if(predicateProof)return done("Native automatic read-to-write recovery verified.");
                check(session.revision==baseline+1&&team.snapshot().roles[0].stepsApplied==1,"recovery reaches a real new native geometry checkpoint");return call("native_finish_recovery","team_finish_goal",R"({"state":"completed","summary":"Resolved the actual native reference and published the next checked editable body."})");
            default:return done("Native read-to-write recovery verified.");
            }
            V edit=json(R"({"expected_revision":0,"commands":[{"op":"feature.edit","id":1,"p":{"height":9}}]})");edit.set("expected_revision",V::makeNum(baseline));return call("native_actual_next_geometry","doc_apply",edit.dump());
        };
        team.start(config,"Recover synthetic native reference and continue editable geometry",findRecovery?reducedReadTools041d():ark::AgentSession::tools(),[&](const auto& tool,const V& args){if(tool=="doc_apply")++writes;return session.call(tool,args);});complete(team);
        const auto snapshot=team.snapshot(true,0,true);check(snapshot.roles[0].state==ark::AgentTeamState::Idle&&snapshot.goalState==ark::AgentGoalState::Completed&&snapshot.unresolvedErrors==0&&writes==1&&session.revision==baseline+1,("real native failure -> current reference -> actual geometry write: "+snapshot.roles[0].error).c_str());
        const auto inspected=session.call("native_inspect",json(R"({"id":1,"detail":"properties"})"));check(!inspected.find("isError")->b&&session.document.features[0].p.at("height")=="9","final native body remains inspectable with the intended edited height");const auto& native=*inspected.find("structuredContent")->find("report");check(native.find("brep_valid")->b&&native.find("solids")->num==1&&std::abs(native.find("volume_mm3")->num-720)<1e-6,"actual recovered native body is one valid exact solid with measured720 cubic millimetres");
        std::cout<<"AGENT_NATIVE_RECOVERY_041D selector="<<selection<<" predicate="<<(predicateProof?"verified":"author_confirmed")<<" final_revision="<<session.revision<<" writes="<<writes<<" volume_mm3="<<native.find("volume_mm3")->num<<'\n';
    }
}
#else
static void nativeReadThenGeometry041d(){}
#endif
