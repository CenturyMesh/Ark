// Included inside ProjectMemory::Impl. A read-only, reversible representation,
// not a model-generated summary or a promotion of historical claims.
V distill(const V& args,const std::string& scope){
    fields(args,{"keys","expected_revisions","max_bytes","current_revision","current_sources"});
    readEvidence(args,scope);
    auto maxBytes=config.maxResultBytes;if(auto p=args.find("max_bytes"))maxBytes=(std::size_t)integer(*p,"max_bytes",1024,config.maxResultBytes);
    Transaction snapshot(db,false);const auto records=selected(args,scope);
    V rows=V::makeArr(),extracts=V::makeObj(),full=V::makeArr();
    std::map<std::string,std::map<std::string,V>> declarations;
    for(const auto& record:records){
        auto row=materialize(scope,record,args);full.arr.push_back(row);
        V payload=V::makeObj();for(auto key:{"content","facts","steps"})payload.set(key,required(row,key));
        const auto extractId=iterationSha256(payload.dump());
        // A digest only identifies an exact payload; never merge a collision.
        if(const auto* existing=extracts.find(extractId.c_str()))need(existing->dump()==payload.dump(),"extraction fingerprint collision");
        else extracts.set(extractId,payload);
        for(const auto& fact:required(payload,"facts").arr){
            V identity=V::makeObj();identity.set("subject",required(fact,"subject"));identity.set("predicate",required(fact,"predicate"));
            auto& claim=declarations[identity.dump()][required(fact,"value").dump()];
            if(claim.isNull()){claim=V::makeObj();claim.set("value",required(fact,"value"));claim.set("record_ids",V::makeArr());}
            auto ids=required(claim,"record_ids");const auto duplicate=std::any_of(ids.arr.begin(),ids.arr.end(),[&](const auto& id){return id.str==record.id;});
            if(!duplicate)ids.arr.push_back(V::makeStr(record.id));claim.set("record_ids",ids);
        }
        row.obj.erase(std::remove_if(row.obj.begin(),row.obj.end(),[](const auto& field){return field.first=="content"||field.first=="facts"||field.first=="steps";}),row.obj.end());
        row.set("extract_id",V::makeStr(extractId));row.set("facts_included",V::makeBool(false));row.set("facts_in_extract",V::makeBool(true));rows.arr.push_back(std::move(row));
    }
    V disagreements=V::makeArr();for(const auto& group:declarations)if(group.second.size()>1){
        auto conflict=parse(group.first);V claims=V::makeArr();for(const auto& claim:group.second)claims.arr.push_back(claim.second);
        conflict.set("claims",claims);conflict.set("status",V::makeStr("conflicting_historical_declarations_require_current_evidence"));disagreements.arr.push_back(conflict);
    }
    V out=envelope(scope);out.set("representation",V::makeStr("ark.memory-exact-distillation.v1"));
    out.set("method",V::makeStr("Exact shared content/facts/ordered steps; each source, outcome, freshness and attribution remains separate. Reconstruct each record by joining its extract_id. No inferred summary, deletion or promotion."));
    out.set("records",rows);out.set("extracts",extracts);out.set("disagreements",disagreements);
    out.set("selected_records",V::makeNum((double)records.size()));out.set("unique_extracts",V::makeNum((double)extracts.obj.size()));
    out.set("full_records_bytes",V::makeNum((double)full.dump().size()));out.set("partial",V::makeBool(false));out.set("storage_changed",V::makeBool(false));
    need(out.dump().size()<=maxBytes,"distillation exceeds max_bytes; select fewer records or explicitly increase max_bytes. No partial distillation returned");
    snapshot.commit();return out;
}
