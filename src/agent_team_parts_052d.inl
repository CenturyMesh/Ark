// Included inside AgentTeam::Impl. All workspace and source executor operations
// share toolsMutex; only the owning role consumes the private conversation.
V partReceipt(const PartTask& task) const {
    V out=V::makeObj();out.set("task",V::makeStr(task.id));out.set("state",V::makeStr(task.state));
    out.set("source_revision",V::makeNum((double)task.sourceRevision));
    out.set("local_revision",V::makeNum((double)task.session->revision));
    out.set("source_published",V::makeBool(task.state=="integrated"));return out;
}
V partCall(std::size_t index,const V& args){
    if(!config.isolatePartDesigners||!config.focusedWorkflow)return errorResult("Part workspaces require the configured Focused isolated-part workflow.");
    const auto action=field(args,"action"),lead=leadRoleId(config.roles);
    const bool isLead=roles[index].view.id==lead;
    std::unique_lock<std::timed_mutex> serial(toolsMutex,std::defer_lock);
    while(!serial.try_lock_for(std::chrono::milliseconds(20)))checkpoint(index);checkpoint(index);
    const auto inheritedControl=captureNativeRequestControl();
    NativeRequestControlScope nativeControl([this,inheritedControl]{return cancelled.load()||(inheritedControl.cancelled&&inheritedControl.cancelled());},inheritedControl.limitMs);
    {std::lock_guard lock(mutex);if(roles[index].steeringVersion!=roles[index].requestVersion)return errorResult("Consume pending steering before operating on a part workspace.");}
    if(action=="assign"){
        if(!isLead||!revision(args)||!args.find("node")||!hasText(field(args,"message")))return errorResult("Only the lead may assign an existing container: to, node, expected_revision, message and optional checks.");
        const auto target=field(args,"to"),brief=field(args,"message");std::size_t recipient=roles.size();
        {std::lock_guard lock(mutex);for(std::size_t i=0;i<roles.size();++i)if(roles[i].view.id==target&&roles[i].isolatedPart)recipient=i;
            if(recipient==roles.size())return errorResult("Choose an isolated PartDesigner role.");
            for(const auto& other:roles)if(other.part&&(other.part->state=="editing"||other.part->state=="submitted"))return errorResult("Focused delegation integrates or discards the current private task before checking out the next part.");
            const auto& role=roles[recipient];if(role.active||role.queued||!role.pending.empty()||(role.part&&role.part->state!="integrated"&&role.part->state!="discarded"))return errorResult("The specialist still owns a task; integrate or explicitly discard it before assigning another.");
            if(partSerial>=config.maxProposals)return errorResult("Part task receipt capacity reached.");
        }
        if(brief.size()>config.maxMailboxBytes/2)return errorResult("Part brief exceeds the mailbox bound.");
        V checks=V::makeArr();if(const auto* requested=args.find("checks")){if(requested->t!=V::Arr||requested->arr.size()>32)return errorResult("Assignment checks must be an array of at most 32 native postconditions.");checks=*requested;}
        if(!executor)return errorResult("Part assignment requires the source executor.");
        V query=V::makeObj();query.set("action",V::makeStr("checkout"));query.set("node",*args.find("node"));query.set("expected_revision",*args.find("expected_revision"));
        const V checked=executor("design_containers",query);if(isError(checked))return checked;
        const auto& data=payload(checked);const auto* root=data.find("document"),*manifest=data.find("manifest");std::uint64_t observed=0;
        if(!root||!manifest||!receiptRevision(data,observed)||observed!=(std::uint64_t)args.find("expected_revision")->num)return errorResult("Checkout did not attest the requested source revision and ownership.");
        auto task=std::make_unique<PartTask>();task->sourceRevision=observed;task->message=brief;task->checks=checks;task->session=std::make_unique<AgentSession>();
        task->session->suppressFailureCapture();task->session->suppressIterationHistory();
        std::string error;if(!documentFromJson(root->dump(),task->session->document,&error))return errorResult("Private checkout rejected: "+error);
        task->session->document.recompute();
        // The full checkout stays in the host. The model gets only scope and a
        // brief, then explicitly queries its local document as needed.
        V envelope=V::makeObj();envelope.set("message",V::makeStr(brief));envelope.set("source_revision",V::makeNum((double)observed));envelope.set("local_revision",V::makeNum(0));
        for(const char* key:{"editable","readonly"})if(const auto* value=manifest->find(key))envelope.set(key,*value);
        envelope.set("acceptance_checks",checks);
        if(envelope.dump().size()>config.maxMailboxBytes)return errorResult("Part scope exceeds the mailbox bound; select a smaller container.");
        V result;
        {std::lock_guard lock(mutex);if(cancelled||roles[index].steeringVersion!=roles[index].requestVersion)return errorResult("Assignment cancelled or superseded before delivery.");
            auto& role=roles[recipient];task->id="part-"+std::to_string(++partSerial);task->epoch=preparedProjectEpoch;envelope.set("task",V::makeStr(task->id));
            role.part=std::move(task);role.view.conversation=role.partInstructions;role.context.clear();role.contextBytes=0;
            role.view.partTask=role.part->id;role.view.partState="editing";
            role.unresolvedToolFailures.clear();role.readFailureStamps.clear();role.successfulReads.clear();role.pendingFeedback.clear();role.lastToolFailure=V{};
            role.view.taskState="in_progress";role.view.taskSummary=brief;role.view.error.clear();role.view.needsInspection=false;role.reportedToCoordinator=false;
            const auto incoming=message("user","Assigned part workspace: "+envelope.dump());role.pending.push_back(incoming);role.pendingBytes=incoming.dump().size();++role.mailVersion;
            roles[index].focusedHandoff=true;goalState=AgentGoalState::InProgress;goalDeclared=false;queue(recipient);
            result=partReceipt(*role.part);result.set("context_bytes",V::makeNum(envelope.dump().size()));result.set("handoff_required",V::makeBool(true));
        }
        wake.notify_all();return result;
    }
    std::size_t owner=index;
    if(isLead){owner=roles.size();std::lock_guard lock(mutex);for(std::size_t i=0;i<roles.size();++i)if(roles[i].part&&roles[i].part->id==field(args,"task"))owner=i;}
    if(owner>=roles.size()||!roles[owner].part)return errorResult("No matching part assignment. The lead must assign a container first.");
    auto& task=*roles[owner].part;
    if(!isLead&&(!roles[index].isolatedPart||(!field(args,"task").empty()&&field(args,"task")!=task.id)))return errorResult("A specialist may access only its own current part task.");
    {std::lock_guard lock(mutex);if(task.epoch!=preparedProjectEpoch)return errorResult("Part workspace belongs to a replaced source session; discard and assign again.");}
    if(action=="inspect")return partReceipt(task);
    if(action=="discard"){
        if(!isLead)return errorResult("Only the lead can discard a private result.");
        if(task.state=="integrated")return errorResult("This task is already published. Discard cannot undo source history.");
        std::lock_guard lock(mutex);task.state="discarded";roles[owner].view.partState=task.state;roles[owner].view.taskState="completed";roles[owner].view.taskSummary="Lead explicitly discarded private work; source unchanged.";return partReceipt(task);
    }
    if(action=="review"||action=="integrate"){
        if(!isLead||!revision(args)||task.state!="submitted")return errorResult("Only the lead can review/integrate a submitted task at an explicit source revision.");
        if((std::uint64_t)args.find("expected_revision")->num!=task.sourceRevision)return errorResult("Source revision differs from the assignment. Reassign explicitly; no automatic rebase.");
        if(!executor)return errorResult("Source executor unavailable.");
        {std::lock_guard lock(mutex);auto busy=writeReservationBusyLocked(index);if(isError(busy))return busy;}
        V child;if(!arkjson::parse(documentToJson(task.session->document,false,false),child))return errorResult("Unable to serialize private authoring.");
        V merge=V::makeObj();merge.set("action",V::makeStr("merge"));merge.set("preview",V::makeBool(true));merge.set("document",child);merge.set("expected_revision",*args.find("expected_revision"));
        if(action=="integrate"){
            if(task.reviewedHash.empty()||field(args,"reviewed_child_sha256")!=task.reviewedHash||task.reviewedRevision!=task.sourceRevision)return errorResult("Review this exact child first and pass its reviewed_child_sha256.");
            merge.set("expected_child_sha256",V::makeStr(task.reviewedHash));
        }
        const auto proposed=executor("design_containers",merge);if(isError(proposed))return proposed;
        const auto& proposal=payload(proposed);const auto* commands=proposal.find("commands");const auto hash=field(proposal,"child_sha256");
        if(!commands||commands->t!=V::Arr||hash.empty())return errorResult("Source merge returned no explicit recipe/digest.");
        V edit=V::makeObj();edit.set("expected_revision",*args.find("expected_revision"));edit.set("commands",*commands);if(!task.checks.arr.empty())edit.set("checks",task.checks);edit.set("detail",V::makeStr("changes"));
        {std::lock_guard lock(mutex);if(cancelled||roles[index].steeringVersion!=roles[index].requestVersion)return errorResult("Review/publication superseded before source execution.");}
        const auto result=executor(action=="review"?"doc_preview":"doc_apply",edit);
        if(isError(result))return result;
        if(action=="review"){
            const auto* valid=payload(result).find("valid");if(!valid||valid->t!=V::Bool||!valid->b)return result;
            {std::lock_guard lock(mutex);task.reviewedHash=hash;task.reviewedRevision=task.sourceRevision;}
            V out=partReceipt(task);out.set("reviewed_child_sha256",V::makeStr(hash));out.set("proposal",proposal);out.set("validation",payload(result));return out;
        }
        std::uint64_t current=0;if(!receiptRevision(payload(result),current)||current<task.sourceRevision)return errorResult("Publication returned no measured revision; inspect the source before continuing.");
        {std::lock_guard lock(mutex);task.state="integrated";roles[owner].view.partState=task.state;roles[owner].view.taskState="completed";roles[owner].view.taskSummary="Private detail integrated by lead at source revision "+std::to_string(current);documentRevision=current;documentRevisionKnown=true;if(current>task.sourceRevision)++roles[index].view.stepsApplied;}
        V out=partReceipt(task);out.set("publication",payload(result));return out;
    }
    if(isLead)return errorResult("Use assign, inspect, review, integrate or discard for a specialist's workspace.");
    if(task.state!="editing")return errorResult("The private result is frozen after submission; ask the lead to integrate or reassign.");
    if(action=="report"||action=="submit"){
        const auto text=field(args,"message");if(!hasText(text)||text.size()>config.maxEventTextBytes)return errorResult("Provide a concise result or concrete missing interface/context.");
        if(action=="submit"){
            if(!revision(args)||(std::uint64_t)args.find("expected_revision")->num!=task.session->revision)return errorResult("Submission requires the inspected current local revision.");
            std::lock_guard lock(mutex);task.state="submitted";roles[index].view.partState=task.state;roles[index].view.taskState="in_progress";roles[index].view.taskSummary="Private detail submitted; lead integration pending.";
        }
        const bool sent=deliver(roles[index].view.id,lead,"Part task "+task.id+" "+(action=="submit"?"submitted for team_part review/integrate":"needs lead input")+": "+text+". Source revision "+std::to_string(task.sourceRevision)+"; no source change by specialist.",false);
        V out=partReceipt(task);out.set("delivered",V::makeBool(sent));return out;
    }
    std::string tool;V local=V::makeObj();
    if(action=="query"){
        tool=field(args,"tool");static const std::set<std::string> allowed{"doc_info","object_context","sketch_inspect","sketch_solve","reference_inspect","geometry_query","physics_inspect","assembly_info","materials_inspect","eval_expr","math_eval","agent_onboard","native_inspect","mesh_inspect","plane_inspect"};
        if(!allowed.count(tool))return errorResult("Query is outside the private part read allowlist; ask the lead for additional context.");
        if(const auto* supplied=args.find("args")){if(supplied->t!=V::Obj)return errorResult("query args must be an object.");local=*supplied;}
    }else if(action=="apply"||action=="preview"){
        if(!revision(args))return errorResult("Private authoring requires the explicit local expected_revision.");
        local.set("expected_revision",*args.find("expected_revision"));
        for(const char* key:{"commands","checks","candidate_id"})if(const auto* value=args.find(key))local.set(key,*value);
        if(args.find("commands")){const auto error=commandsError(local,config.maxCommandsPerStep,AgentTeamDiscipline::General,config.maxFeaturesPerStep,config.maxPlacementsPerStep);if(!error.empty())return errorResult(error);}
        tool=action=="apply"?"doc_apply":"doc_preview";local.set("detail",V::makeStr("changes"));
    }else return errorResult("Unknown part action.");
    // No fallback to the parent executor: the retained checkout manifest enforces
    // read-only dependencies and confines edits through ordinary CAD validation.
    const auto before=task.session->revision;V result=task.session->call(tool,local);if(!isError(result)&&task.session->revision>before){std::lock_guard lock(mutex);++roles[index].view.privateStepsApplied;}result.set("part_workspace",partReceipt(task));return result;
}
