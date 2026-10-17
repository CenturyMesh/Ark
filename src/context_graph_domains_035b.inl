// Additional navigation nodes are derived solely from authored project fields.
// Vendor approval/account settings are deliberately not read into project context.
std::string graphPublicUrl035B(const std::string& url){
    if(url.rfind("https://",0)!=0||url.size()>4096||url.find('@')!=std::string::npos)return {};
    const auto lower=contextLower(url);for(const char* secret:{"token=","api_key=","apikey=","password=","signature=","credential=","secret=","authorization="})if(lower.find(secret)!=std::string::npos)return {};
    return url;
}
std::string graphSource035B(ProjectGraph& graph,const std::string& owner,const std::string& original,const std::string& relation){
    const auto url=graphPublicUrl035B(original);if(url.empty())return {};
    auto end=url.find_first_of("/?#",8);const auto host=contextLower(url.substr(8,end==std::string::npos?std::string::npos:end-8));if(host.empty())return {};
    V source=V::makeObj();source.set("name",V::makeStr(url));source.set("url",V::makeStr(url));source.set("host",V::makeStr(host));source.set("authority",V::makeStr("caller-authored source link; product match, bytes and vendor approval require separate inspection"));
    const auto key="source:"+contextFingerprint(V::makeStr(url));graph.node(key,"source_link",source);
    V vendor=V::makeObj();vendor.set("name",V::makeStr(host));vendor.set("host",V::makeStr(host));vendor.set("authority",V::makeStr("source URL host; approval/account status is not inferred"));const auto vendorKey="vendor:"+host;graph.node(vendorKey,"vendor",vendor);graph.edge(owner,relation,key,"authored_provenance");graph.edge(key,"hosted_by",vendorKey,"authored_url_host");return key;
}
void graphDomains035B(ProjectGraph& graph){
    // Freeze the existing authoring list: adding derived nodes must not recurse.
    const auto originals=graph.source;
    for(const auto& item:originals){const auto& owner=item.first;const auto& authored=item.second;if(owner.rfind("feature:",0)!=0)continue;
        const auto* native=authored.find("native");const auto* metadata=native?native->find("component_metadata"):nullptr;
        for(const auto* provenance:{authored.find("part"),metadata})if(provenance)for(const char* field:{"source_url","datasheet_url"})graphSource035B(graph,owner,graphText(*provenance,field),field==std::string("source_url")?"sourced_from":"documented_by");
        if(metadata)if(const auto* evidence=metadata->find("download_evidence")){const auto sha=graphText(*evidence,"sha256");if(sha.size()==64&&sha.find_first_not_of("0123456789abcdef")==std::string::npos){V row=V::makeObj(),facts=V::makeObj();row.set("name",V::makeStr("STEP "+sha.substr(0,16)));for(const char* field:{"sha256","byte_size","http_status","retrieved_at_utc","content_type"})if(const auto* value=evidence->find(field))facts.set(field,*value);if(const auto* policy=evidence->find("source_policy")){V prior=V::makeObj();for(const char* field:{"vendor","observed_vendor","approved","policy_revision","policy_sha256"})if(const auto* value=policy->find(field))prior.set(field,*value);facts.set("network_policy_at_retrieval",prior);}row.set("binding",facts);row.set("authority",V::makeStr("stored retrieval metadata; compare original STEP SHA256/native custody; not current approval, supplier match or manufacturing qualification"));const auto key="step_artifact:"+sha;graph.node(key,"step_artifact",row);graph.edge(owner,"retains_step_artifact",key,"stored_source_metadata");graphSource035B(graph,key,graphText(*evidence,"final_url"),"downloaded_from");}}
        if(!native)continue;
        if(graphText(authored,"type")=="cable_route")for(const char* end:{"start","end"})if(const auto* binding=native->find(end)){
            V endpoint=V::makeObj(),safe=*binding;if(binding->t==V::Obj){safe=V::makeObj();for(const char* field:{"node","copy","reference","offset","name","point","connector","pin","net"})if(const auto* value=binding->find(field))safe.set(field,contextCompact(*value));}auto label=graphText(*binding,"connector");if(label.empty())label=graphText(*binding,"name");if(label.empty())label=graphText(authored,"name")+" "+end;endpoint.set("name",V::makeStr(label));endpoint.set("binding",safe);endpoint.set("authority",V::makeStr("authored cable endpoint; net/pin names do not prove electrical continuity"));
            const auto key="endpoint:"+owner+":"+end;graph.node(key,"connector_endpoint",endpoint);graph.edge(owner,end==std::string("start")?"routed_from":"routed_to",key,"authored_endpoint");
            if(const auto* node=binding->find("node");node&&node->t==V::Num&&std::isfinite(node->num)&&node->num>0&&node->num<=9007199254740991.&&node->num==std::floor(node->num))graph.edge(key,"attaches_to","assembly:"+std::to_string((u64)node->num),"authored_occurrence_reference");
        }
        if(graphText(authored,"type")=="pcb"){
            if(const auto* components=native->find("components");components&&components->t==V::Arr)for(const auto& component:components->arr){const auto ref=graphText(component,"ref");if(ref.empty())continue;V row=V::makeObj();row.set("name",V::makeStr(ref+" "+graphText(component,"value")));row.set("ref",V::makeStr(ref));row.set("binding",contextCompact(component));row.set("authority",V::makeStr("authored PCB component envelope, not a verified supplier model"));const auto key="pcb_component:"+owner+":"+ref;graph.node(key,"pcb_component",row);graph.edge(owner,"contains_component",key,"authored_pcb_program");graphSource035B(graph,key,graphText(component,"source_url"),"sourced_from");}
            for(const char* section:{"pads","traces","vias"})if(const auto* entries=native->find(section);entries&&entries->t==V::Arr)for(const auto& entry:entries->arr){const auto net=graphText(entry,"net");if(net.empty())continue;V row=V::makeObj();row.set("name",V::makeStr(net));row.set("net",V::makeStr(net));row.set("authority",V::makeStr("authored net scoped to this PCB; inspect electronics connectivity"));const auto key="pcb_net:"+owner+":"+net;graph.node(key,"pcb_net",row);graph.edge(owner,"declares_net",key,"authored_pcb_program");}
        }
    }
}
