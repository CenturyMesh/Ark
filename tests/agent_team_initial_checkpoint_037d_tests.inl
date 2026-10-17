// A.04/D.02: offline provider boundaries and actual atomic AgentSession edits.
static void initialLeadAuthoringCheckpoint037d(){
    for(bool sketchCheckpoint:{true,false}){
        ark::AgentTeam team;ark::AgentSession session;session.suppressIterationHistory();
        ark::AgentTeamConfig config;config.requireLeadAuthoringCheckpoint=true;config.orchestratedStart=true;config.maxWorkers=3;
        config.roles={role("engineer",true),role("designer",true),role("reviewer")};
        config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
        config.roles[1].discipline=ark::AgentTeamDiscipline::PartDesigner;
        config.roles[2].discipline=ark::AgentTeamDiscipline::PhysicsReviewer;
        Gate beforeGeometry,parallel,review;int lead=0;std::atomic_int designer=0,reviewer=0,designerWrites=0;
        config.roles[0].transport=[&](auto&,const V& request,auto*){
            switch(++lead){
            case 1:return call("checkpoint_assign","team_send",R"({"to":"designer","kind":"assignment","message":"Create the assigned bracket after the lead checkpoint; preserve interface datum."})");
            case 2:{const auto receipt=semanticTeamOutput036a(request);check(receipt.find("delivered")->b&&!receipt.find("handoff_required")->b&&receipt.find("recipients")->arr[0].find("state")->asStr()=="deferred"&&receipt.find("scheduling")->asStr().find("lead must author first coherent editable checkpoint")!=std::string::npos,"Parallel assignment explicitly retains deferred mail without Focused yield");return call("checkpoint_mail","team_send",R"({"to":"designer","kind":"message","message":"Retain this second interface note."})");}
            case 3:return call("checkpoint_review","team_send",R"({"to":"reviewer","kind":"assignment","message":"Review the explicit concept assumptions without writing."})");
            case 4:review.await(1);review.open();return call("checkpoint_context","team_context","{}");
            case 5:{const auto receipt=semanticTeamOutput036a(request);check(designer==0&&receipt.find("initial_authoring_checkpoint")->find("state")->asStr()=="pending"&&receipt.find("roles")->arr[1].find("pending_messages")->num==2&&receipt.find("roles")->arr[1].find("deferred_until_lead_checkpoint")->b&&receipt.find("next_action")->find("action")->asStr()=="author_initial_lead_checkpoint","context reports retained assignments and requires lead authoring while reviewer runs");return call("checkpoint_params","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"concept_width","expr":12}]})");}
            case 6:check(designer==0,"parameter-only successful commit does not wake a designer");return call("checkpoint_group","team_apply_step",R"({"expected_revision":1,"commands":[{"op":"assembly.add","name":"Concept group","feature":0}]})");
            case 7:check(designer==0,"group-only successful commit does not wake a designer");return call("checkpoint_rejected","team_apply_step",R"({"expected_revision":2,"commands":[{"op":"feature.add","name":"Invalid checkpoint","type":"box","p":{"dx":"missing_checkpoint_dimension","dy":3,"dz":2}}]})");
            case 8:check(designer==0&&semanticTeamOutput036a(request).find("error"),"failed authoring does not release deferred writers");beforeGeometry.enter();return call("checkpoint_author",sketchCheckpoint?"team_apply_step":"doc_apply",sketchCheckpoint?R"({"expected_revision":2,"commands":[{"op":"sketch.set","name":"Lead profile","sketch":{"plane":"XY","entities":[{"kind":"line","name":"edge","dof":[0,0,12,0]}],"constraints":[]}}]})":R"({"expected_revision":2,"commands":[{"op":"feature.add","name":"Lead concept","type":"box","p":{"dx":"concept_width","dy":6,"dz":3}}]})");
            case 9:parallel.enter();return done("Lead checkpoint is committed; independent Parallel work can continue.");
            default:return done("Actual specialist turn notification retained.");
            }
        };
        config.roles[1].transport=[&](auto&,const V& request,auto*){
            switch(++designer){
            case 1:check(request.find("input")->dump().find("assigned bracket")!=std::string::npos&&request.find("input")->dump().find("second interface note")!=std::string::npos&&request.find("input")->dump().find("Use the authored interface")!=std::string::npos,"deferred writer receives all assignment, message and user steering mail");parallel.enter();return call("checkpoint_old_write","doc_apply",R"({"expected_revision":0,"commands":[{"op":"param.set","name":"designer_old","expr":99}]})");
            case 2:check(semanticTeamOutput036a(request).find("error")->asStr().find("Read current doc_info successfully")!=std::string::npos,"released writer must freshly inspect rather than replay or rebase an old edit");return call("checkpoint_fresh_read","doc_info",R"({"detail":"revision"})");
            case 3:check(semanticTeamOutput036a(request).find("revision")->num==3,"released writer observes the actual checkpoint revision");return call("checkpoint_remaining_write","team_apply_step",R"({"expected_revision":3,"commands":[{"op":"param.set","name":"designer_current","expr":7}]})");
            default:return done("Current revision inspected and the remaining assigned edit committed.");
            }
        };
        config.roles[2].transport=[&](auto&,auto&,auto*){++reviewer;review.enter();return done("Read-only assumptions review; no authored geometry claimed.");};
        team.start(config,"Create an editable concept then work in Parallel",ark::AgentSession::tools(),[&](const auto& name,const V& args){if(name=="doc_apply"&&ark::AgentTeam::currentRoleId()=="designer")++designerWrites;return session.call(name,args);});
        beforeGeometry.await(1);check(designer==0&&reviewer==1&&session.revision==2,"no designer provider/thread request before first valid lead geometry, while reviewer remains available");
        check(team.steer("Use the authored interface","designer"),"user steering is accepted while writer is deferred");beforeGeometry.open();parallel.await(2);
        auto concurrent=team.snapshot();check(concurrent.roles[0].state==ark::AgentTeamState::Working&&concurrent.roles[1].state==ark::AgentTeamState::Working,"lead and designer provider work run concurrently after checkpoint");parallel.open();complete(team);
        check(designer==4&&designerWrites==1&&session.revision==4&&session.document.getParam("designer_current")==7&&team.snapshot().roles[1].unresolvedToolErrors==0,"first lead sketch/feature releases exactly one retained writer turn with fresh atomic edit");
    }
}

static void initialCheckpointLifecycle037d(){
    ark::AgentTeam team;ark::AgentTeamConfig config;check(!config.requireLeadAuthoringCheckpoint,"external/core teams keep checkpoint gating disabled by default");
    config.requireLeadAuthoringCheckpoint=true;config.orchestratedStart=true;config.roles={role("engineer",true),role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    int lead=0;std::atomic_int designers=0;config.roles[0].transport=[&](auto&,auto&,auto*){if(++lead==1)return call("unfinished_assignment","team_send",R"({"to":"designer","message":"Retain this unfinished component assignment."})");return done("Lead stopped without authoring geometry.");};
    config.roles[1].transport=[&](auto&,auto&,auto*){++designers;return done("Explicitly assigned work received.");};
    team.start(config,"Start an empty concept",ark::AgentSession::tools(),{});complete(team);auto unfinished=team.snapshot(true);
    check(designers==0&&!unfinished.running&&unfinished.goalState==ark::AgentGoalState::NeedsInput&&unfinished.goalSummary.find("checkpoint")!=std::string::npos&&unfinished.roles[0].status.find("unfinished")!=std::string::npos&&unfinished.roles[1].taskState=="in_progress","lead exit leaves a truthful unfinished checkpoint with no hidden provider continuation");
    check(team.steer("Additional queued user requirement","designer"),"deferred input remains an accepted user action");complete(team);check(designers==0,"steering alone does not bypass initial authoring gate");team.cancel();complete(team);auto cancelled=team.snapshot(true);
    check(cancelled.roles[1].state==ark::AgentTeamState::Cancelled&&cancelled.roles[1].conversation.dump().find("unfinished component assignment")!=std::string::npos&&cancelled.roles[1].conversation.dump().find("Additional queued user requirement")!=std::string::npos,"cancellation clears deferred scheduling and retains accepted mail custody");
    ark::AgentSession restarted;lead=0;
    config.roles[0].transport=[&](auto&,auto&,auto*){switch(++lead){case 1:return call("restart_assign","team_send",R"({"to":"designer","message":"New session component assignment."})");case 2:return call("restart_checkpoint","doc_apply",R"({"expected_revision":0,"commands":[{"op":"feature.add","name":"Restarted concept","type":"box","p":{"dx":5,"dy":4,"dz":3}}]})");default:return done();}};
    config.roles[1].transport=[&](auto&,const V& request,auto*){++designers;check(request.find("input")->dump().find("unfinished component assignment")==std::string::npos,"restart does not leak cancelled pending assignments");return done();};
    team.start(config,"Restart with a new concept",ark::AgentSession::tools(),[&](auto& name,auto& args){return restarted.call(name,args);});complete(team);check(designers==1&&restarted.revision==1,"restart resets the checkpoint gate and releases only after new session geometry");
    config.focusedWorkflow=true;lead=0;designers=0;config.roles[0].transport=[&](auto&,auto&,auto*){if(++lead==1)return call("focused_assign","team_send",R"({"to":"designer","message":"Inspect this explicit read-only task."})");return done();};config.roles[1].transport=[&](auto&,auto&,auto*){++designers;return done();};
    team.start(config,"Focused workflow remains sequential",ark::AgentSession::tools(),{});complete(team);check(designers==1,"Focused handoff remains unchanged even if the optional Parallel flag is set");
}

static void coherentConstructionBounds037d(){
    ark::AgentTeam team;ark::AgentSession session;session.suppressIterationHistory();ark::AgentTeamConfig config;
    check(config.maxCommandsPerStep==8&&config.maxFeaturesPerStep==8&&config.maxPlacementsPerStep==8,"existing construction defaults remain unchanged");
    config.maxCommandsPerStep=128;config.maxFeaturesPerStep=32;config.maxPlacementsPerStep=64;config.roles={role("engineer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    V commands=json(R"([{"op":"param.set","name":"batch_width","expr":10},{"op":"param.set","name":"batch_depth","expr":6},{"op":"param.set","name":"batch_height","expr":3},{"op":"sketch.set","name":"Batch profile","sketch":{"plane":"XY","entities":[{"kind":"line","name":"edge","dof":[0,0,10,0]}],"constraints":[]}}])");
    for(int i=0;i<9;++i){auto feature=json(R"({"op":"feature.add","type":"box","p":{"dx":"batch_width","dy":"batch_depth","dz":"batch_height"}})");feature.set("name",V::makeStr("Batch part "+std::to_string(i)));commands.arr.push_back(feature);auto placement=json(R"({"op":"assembly.add"})");placement.set("name",V::makeStr("Batch instance "+std::to_string(i)));placement.set("feature",V::makeStr("Batch part "+std::to_string(i)));commands.arr.push_back(placement);}
    int rounds=0,writes=0;config.roles[0].transport=[&](auto&,auto&,auto*){if(++rounds!=1)return done("One coherent editable construction batch committed.");V args=V::makeObj();args.set("expected_revision",V::makeNum(0));args.set("commands",commands);return call("large_coherent_step","team_apply_step",args.dump());};
    team.start(config,"Create nine explicit fixture parts coherently",ark::AgentSession::tools(),[&](auto& name,auto& args){if(name=="doc_apply")++writes;return session.call(name,args);});complete(team);
    check(writes==1&&session.revision==1&&session.document.features.size()==9&&session.document.assembly.size()==9&&session.document.sketches.size()==1&&team.snapshot().roles[0].stepsApplied==1,"22-command step with nine features and placements commits atomically through actual AgentTeam/AgentSession");
    const auto undo=session.call("doc_undo",json(R"({"expected_revision":1})"));check(!undo.find("isError")->b&&session.revision==2&&session.document.features.empty()&&session.document.assembly.empty()&&session.document.sketches.empty(),"one Undo removes the whole coherent batch");
    check(session.call("doc_undo",json(R"({"expected_revision":2})")).find("isError")->b,"coherent batch has exactly one undo entry");
}

static V initialCheckpointRawOutput037d(const V& request){
    for(auto item=request.find("input")->arr.rbegin();item!=request.find("input")->arr.rend();++item)if(item->find("type")&&item->find("type")->asStr()=="function_call_output")return json(item->find("output")->asStr());
    throw std::runtime_error("missing initial checkpoint receipt");
}

static void initialCheckpointRetainedGeometry037d(){
    ark::AgentTeam team;ark::AgentSession session;session.suppressIterationHistory();ark::AgentTeamConfig config;config.requireLeadAuthoringCheckpoint=true;config.orchestratedStart=true;config.roles={role("engineer",true),role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;
    const std::vector<V> emptyBatches={
        json(R"([{"op":"sketch.set","name":"Removed profile","sketch":{"plane":"XY","entities":[{"kind":"line","name":"Edge","dof":[0,0,12,0]}]}},{"op":"sketch.remove","name":"Removed profile"}])"),
        json(R"([{"op":"feature.add","name":"Removed part","type":"box","p":{"dx":8,"dy":4,"dz":2}},{"op":"feature.remove","id":"Removed part"}])"),
        json(R"([{"op":"sketch.set","name":"Retained profile","sketch":{"plane":"XY","entities":[{"kind":"line","name":"Edge","dof":[0,0,12,0]}]}},{"op":"sketch.set","name":"Retained profile","sketch":{"plane":"XY","entities":[]}}])"),
        json(R"([{"op":"sketch.entity.set","name":"Retained profile","entity":{"kind":"line","name":"Edge","dof":[0,0,12,0]}},{"op":"sketch.set","name":"Retained profile","sketch":{"plane":"XY","entities":[]}}])")
    };
    std::atomic_int designers=0;int lead=0,inspectionReads=0;
    config.roles[0].transport=[&](auto&,const V& request,auto*){
        const int round=++lead;if(round==1)return call("retained_assign","team_send",R"({"to":"designer","message":"Continue only after retained editable geometry exists."})");
        if(round>=3&&round<=6){const auto result=initialCheckpointRawOutput037d(request);const auto* verification=result.find("initial_checkpoint_verification");check(result.find("isError")&&!result.find("isError")->b&&semanticTeamOutput036a(request).find("revision")->num==round-2,"create/remove or replace-empty remains a successful atomic commit");check(verification&&verification->find("state")->asStr()=="pending"&&verification->find("reason")->asStr()=="no_retained_authoring_geometry"&&designers==0&&team.snapshot().roles[1].status.find("Deferred")!=std::string::npos,"removed feature/sketch or last sketch entity cannot satisfy the retained geometry checkpoint");}
        if(round>=2&&round<=5){V args=V::makeObj();args.set("expected_revision",V::makeNum(round-2));args.set("commands",emptyBatches[round-2]);return call("retained_empty_"+std::to_string(round),"team_apply_step",args.dump());}
        if(round==6)return call("retained_real_geometry","doc_apply",R"({"expected_revision":4,"commands":[{"op":"sketch.entity.set","name":"Retained profile","entity":{"kind":"line","name":"Persistent edge","dof":[0,0,20,0]}}]})");
        if(round==7){const auto result=initialCheckpointRawOutput037d(request);check(result.find("initial_checkpoint_verification")&&result.find("initial_checkpoint_verification")->find("state")->asStr()=="verified","retained geometry at the committed revision verifies the initial checkpoint");}
        return done("Only the final retained sketch releases the writer.");
    };
    config.roles[1].transport=[&](auto&,auto&,auto*){if(++designers==1)return call("retained_revision","doc_info","{}");return done("Retained sketch observed.");};
    team.start(config,"Retain a real initial editable checkpoint",ark::AgentSession::tools(),[&](const auto& name,const V& args){if(name=="doc_info"&&args.find("sections")){++inspectionReads;check(ark::AgentTeam::currentRoleId()=="engineer"&&args.find("detail")->asStr()=="design"&&args.find("sections")->dump()==R"(["features","sketches"])","checkpoint proof is a lead-only selected design inspection");}return session.call(name,args);});complete(team);
    check(session.revision==5&&inspectionReads==5&&designers==2&&session.document.features.empty()&&session.document.findSketch("Retained profile")->sk.ents.size()==1,"four net-empty commits keep writers deferred until a fifth actually retained checkpoint");

    for(int failure=0;failure<4;++failure){
        ark::AgentSession recovered;recovered.suppressIterationHistory();lead=0;designers=0;inspectionReads=0;
        config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++lead){case 1:return call("proof_assign","team_send",R"({"to":"designer","message":"Wait for a confirmed retained checkpoint."})");case 2:return call("proof_committed","doc_apply",R"({"expected_revision":0,"commands":[{"op":"feature.add","name":"Committed before unavailable proof","type":"box","p":{"dx":8,"dy":4,"dz":2}}]})");case 3:{const auto result=initialCheckpointRawOutput037d(request);const auto* proof=result.find("initial_checkpoint_verification");check(result.find("isError")&&!result.find("isError")->b&&semanticTeamOutput036a(request).find("revision")->num==1&&team.snapshot().roles[0].stepsApplied==1&&team.snapshot().roles[0].unresolvedToolErrors==0,"unavailable or changed inspection never turns a successful commit into a rejected operation");check(proof&&proof->find("state")->asStr()=="pending"&&designers==0&&team.snapshot().roles[1].status.find("Deferred")!=std::string::npos,"missing, failed or different-revision evidence leaves the checkpoint pending");return call("proof_recovered_commit","team_apply_step",R"({"expected_revision":1,"commands":[{"op":"feature.add","name":"Verified checkpoint","type":"box","p":{"dx":6,"dy":3,"dz":2}}]})");}default:return done("Verified current retained geometry releases the writer.");}};
        team.start(config,"Preserve successful commits when checkpoint inspection is unavailable",ark::AgentSession::tools(),[&](const auto& name,const V& args){
            if(name=="doc_info"&&args.find("sections")&&++inspectionReads==1){if(failure==0)return json(R"({"isError":true,"error":"Read unavailable"})");if(failure==1)throw std::runtime_error("Inspection transport unavailable");V response=recovered.call("doc_info",json(R"({"detail":"design","sections":["features","sketches"]})"));V data=*response.find("structuredContent");data.set("revision",failure==2?V::makeNum(0):V{});response.set("structuredContent",data);return response;}
            return recovered.call(name,args);
        });complete(team);
        check(recovered.revision==2&&recovered.document.features.size()==2&&inspectionReads==2&&designers==2&&team.snapshot().roles[0].stepsApplied==2&&team.snapshot().roles[0].unresolvedToolErrors==0,"later exact revision inspection releases writers without replaying earlier successful commits");
    }
}

static void initialCheckpointNeedsSketchGeometry037d(){
    ark::AgentTeam team;ark::AgentSession session;session.suppressIterationHistory();ark::AgentTeamConfig config;config.requireLeadAuthoringCheckpoint=true;config.orchestratedStart=true;config.roles={role("engineer",true),role("designer",true)};config.roles[0].discipline=ark::AgentTeamDiscipline::Orchestrator;config.roles[1].discipline=ark::AgentTeamDiscipline::PartDesigner;std::atomic_int lead=0,designer=0;
    config.roles[0].transport=[&](auto&,const V& request,auto*){switch(++lead){case 1:return call("empty_assign","team_send",R"({"to":"designer","kind":"assignment","message":"Continue after the lead's first actual sketch line."})");case 2:return call("empty_sketch","doc_apply",R"({"expected_revision":0,"commands":[{"op":"sketch.set","name":"Initial profile","sketch":{"plane":"XY","entities":[]}}]})");case 3:return call("still_empty","team_context","{}");case 4:check(designer==0&&!semanticTeamOutput036a(request).find("initial_authoring_checkpoint")->find("satisfied")->b,"an empty sketch cannot release writers before any editable geometry exists");return call("first_line","doc_apply",R"({"expected_revision":1,"commands":[{"op":"sketch.entity.set","name":"Initial profile","entity":{"kind":"line","name":"Interface","dof":[0,0,20,0]}}]})");default:return done("Initial sketch geometry committed.");}};
    config.roles[1].transport=[&](auto&,auto&,auto*){if(++designer==1)return call("line_revision","doc_info","{}");return done("Initial sketch line observed.");};team.start(config,"Start a new editable concept",ark::AgentSession::tools(),[&](const auto& name,const V& args){return session.call(name,args);});complete(team);check(session.revision==2&&designer>=2&&session.document.findSketch("Initial profile")->sk.ents.size()==1,"a real sketch entity releases deferred writers with current revision evidence");
    initialCheckpointRetainedGeometry037d();
}
