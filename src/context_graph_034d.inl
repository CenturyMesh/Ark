// Read-only graph derived from existing project authoring and attributed audit
// records. Discovery links are navigation evidence, never geometry authority.
namespace {
std::string graphText(const V& value,const char* key){const auto* p=value.find(key);return p&&p->t==V::Str?p->str:std::string();}
std::string graphClip(std::string value,size_t limit=512){if(value.size()<=limit)return value;while(limit&&(static_cast<unsigned char>(value[limit])&0xc0)==0x80)--limit;return value.substr(0,limit);}
std::vector<std::string> graphWords(const std::string& text){std::vector<std::string> words;const auto lower=contextLower(text);auto token=[](unsigned char c){return std::isalnum(c)||c=='_'||c>=128;};for(size_t i=0;i<lower.size();){if(token((unsigned char)lower[i])){const auto start=i++;while(i<lower.size()&&token((unsigned char)lower[i]))++i;words.push_back(lower.substr(start,i-start));}else ++i;}return words;}
struct ProjectGraph {
    std::map<std::string,V> nodes,source;
    std::set<std::tuple<std::string,std::string,std::string,std::string>> edges;
    void node(const std::string& key,const std::string& kind,const V& authored){
        V row=V::makeObj(),facts=V::makeObj();row.set("key",V::makeStr(key));row.set("kind",V::makeStr(kind));row.set("name",V::makeStr(graphText(authored,"name")));source[key]=authored;
        for(const char* field:{"type","expr","p","material","suppressed","fixed","visible","pattern","density","density_g_cm3","url","host","authority","binding","ref","net"})if(const auto* value=authored.find(field))facts.set(field,contextCompact(*value));
        if(const auto* part=authored.find("part")){V metadata=V::makeObj();for(const char* field:{"part_number","description","procurement","specification","manufacturer","manufacturer_part_number","supplier","supplier_part_number","sourcing_status"})if(const auto* value=part->find(field);value&&value->t==V::Str)metadata.set(field,V::makeStr(graphClip(value->str)));facts.set("part",metadata);}
        if(const auto* native=authored.find("native"))if(const auto* op=native->find("op"))facts.set("operation",*op);
        if(const auto* native=authored.find("native")){V program=V::makeObj();for(const char* field:{"route_mode","profile","start_tangent","end_tangent","auto_route"})if(const auto* value=native->find(field))program.set(field,contextCompact(*value));for(const char* field:{"waypoints","obstacles","components","pads","traces","vias"})if(const auto* value=native->find(field);value&&value->t==V::Arr)program.set(std::string(field)+"_count",V::makeNum(value->arr.size()));if(!program.obj.empty())facts.set("program",program);}
        if(facts.dump().size()>2048)facts=contextOmitted(facts,"graph facts exceed 2 KiB; inspect object_context or the source tool");row.set("facts",facts);nodes[key]=row;
    }
    void edge(const std::string& from,const std::string& relation,const std::string& to,const std::string& basis="authored_selector"){
        if(from!=to&&nodes.count(from)&&nodes.count(to))edges.emplace(from,relation,to,basis);
    }
    void scan(const std::string& from,const V& value,const std::string& key=""){
        if(value.t==V::Str){
            if(key=="sketch"||key=="source_sketch"||key=="sections"||key=="guides"||key=="profile"||key=="path"||key=="section")edge(from,"uses_sketch","sketches:"+value.str);
            if(key=="plane"||key=="datum"||key=="parent")edge(from,"uses_plane","planes:"+value.str);
            if(key=="material")edge(from,"uses_material","materials:"+value.str);
            if(key=="name"||key=="reference"||key=="type"||key=="op"||key=="kind"||key=="material")return;
            for(const auto& word:graphWords(value.str))for(const auto& n:nodes)if(n.second.find("kind")->str=="params"&&contextLower(graphText(n.second,"name"))==word)edge(from,"uses_parameter",n.first,"expression_identifier");
        }else if(value.t==V::Num){
            if(!std::isfinite(value.num)||value.num<0||value.num>9007199254740991.||value.num!=std::floor(value.num))return;
            if(key=="feature"||key=="source_feature"||key=="second_feature"||key=="inputs")edge(from,"depends_on",contextKey((u64)value.num));
            if(key=="source_instance"||key=="instances")edge(from,"uses_occurrence","assembly:"+std::to_string((u64)value.num));
        }else if(value.t==V::Arr){for(const auto& item:value.arr)scan(from,item,key);}
        else if(value.t==V::Obj){for(const auto& item:value.obj)if(item.first!="source"&&item.first!="mesh"&&item.first!="part"&&item.first!="appearance")scan(from,item.second,item.first);}
    }
    V finish(const std::string& scope){
        V out=V::makeObj(),rows=V::makeObj(),links=V::makeArr();
        for(const auto& item:edges){V edge=V::makeObj();edge.set("from",V::makeStr(std::get<0>(item)));edge.set("relation",V::makeStr(std::get<1>(item)));edge.set("to",V::makeStr(std::get<2>(item)));edge.set("basis",V::makeStr(std::get<3>(item)));links.arr.push_back(edge);}
        for(auto& item:nodes){V identity=V::makeObj();identity.set("scope",V::makeStr(scope));identity.set("authored",source.at(item.first));V neighbors=V::makeArr();for(const auto& edge:links.arr)if(graphText(edge,"from")==item.first||graphText(edge,"to")==item.first)neighbors.arr.push_back(edge);identity.set("links",neighbors);item.second.set("fingerprint",V::makeStr(contextFingerprint(identity)));rows.set(item.first,item.second);}
        out.set("nodes",rows);out.set("edges",links);out.set("scope",V::makeStr(scope));return out;
    }
};
#include "context_graph_domains_035b.inl"
V buildProjectGraph(const V& authored,const std::string& scope){
    ProjectGraph graph;
    for(const auto& row:authored.find("features")->arr)graph.node(contextKey((u64)row.find("id")->num),"feature",row);
    for(const char* section:{"params","sketches","planes","materials"})if(const auto* rows=authored.find(section))for(const auto& row:rows->arr)graph.node(std::string(section)+":"+graphText(row,"name"),section,row);
    if(const auto* rows=authored.find("assembly"))for(const auto& row:rows->arr)graph.node("assembly:"+std::to_string((u64)row.find("id")->num),"assembly",row);
    if(const auto* rows=authored.find("mates"))for(const auto& row:rows->arr)graph.node("mate:"+std::to_string((u64)row.find("id")->num),"mate",row);
    graphDomains035B(graph);
    for(const auto& item:graph.source){const auto& key=item.first;const auto& row=item.second;graph.scan(key,row);
        if(key.rfind("planes:",0)==0)if(const auto* support=row.find("support");support&&graphText(*support,"kind")=="bisector")for(const char* anchor:{"first","second"})if(const auto* value=support->find(anchor);value&&value->t==V::Str)graph.edge(key,"uses_plane","planes:"+value->str);
        if(key.rfind("assembly:",0)==0){if(const auto* parent=row.find("parent"))graph.edge(key,"in_group","assembly:"+std::to_string((u64)parent->num));if(const auto* feature=row.find("feature"))graph.edge(key,"places",contextKey((u64)feature->num));}
        if(key.rfind("mate:",0)==0)for(const char* end:{"a","b"})if(const auto* node=row.find(end))graph.edge(key,"constrains","assembly:"+std::to_string((u64)node->num));
    }
    return graph.finish(scope);
}
// Superset of the fields written by documentToJson(document, false, false).
// A miss only costs another serialization. A hit must mean that JSON is unchanged.
std::string authoringStamp(const Document& document){
    std::string stamp;
    // Lengths and collection tags/counts make field boundaries unambiguous,
    // including authored strings containing embedded NUL bytes.
    auto add=[&](const std::string& value){stamp.append(std::to_string(value.size()));stamp.push_back(':');stamp.append(value);};
    auto section=[&](const char* name,size_t count){add(name);add(std::to_string(count));};
    auto addNumber=[&](double value){add(V::makeNum(value).dump());}; // Same lossless numeric form as saved JSON.
    auto addMap=[&](const std::unordered_map<std::string,std::string>& values){section("map",values.size());std::vector<std::string> keys;keys.reserve(values.size());for(const auto& item:values)keys.push_back(item.first);std::sort(keys.begin(),keys.end());for(const auto& key:keys){add(key);add(values.at(key));}};
    add(document.bomDocumentId);add(std::to_string(document.nextFeatureId_));add(std::to_string(document.nextAssemblyId_));add(std::to_string(document.nextMateId_));add(std::to_string(document.historyTip));
    add(document.skeleton.dump());add(document.designContainerCheckout.dump());add(document.assemblyConfigurations.dump());add(document.assemblyLoadSelection.dump());add(document.assemblyExternalReferences.dump());
    section("drawings",document.drawings.size());for(const auto& drawing:document.drawings)add(drawing.dump());
    section("params",document.params.size());for(const auto& parameter:document.params){add(parameter.name);add(parameter.expr);}
    section("image_assets",document.imageAssets.size());for(const auto& asset:document.imageAssets)add(imageAssetToJson(asset,true).dump());
    section("stamps",document.stamps.size());for(const auto& row:document.stamps)add(stampToJson(row).dump());
    section("materials",document.materials.size());for(const auto& material:document.materials){add(material.name);addNumber(material.densityGcm3);add(appearanceToJson(material.appearance).dump());add(material.physical.dump());}
    section("planes",document.planes.size());for(const auto& plane:document.planes){add(plane.name);add(plane.parent);add(plane.kind);addMap(plane.p);add(plane.support.dump());}
    section("canvases",document.canvases.size());for(const auto& canvas:document.canvases){add(canvas.name);add(canvas.plane);add(canvas.path);add(canvas.asset);add(canvas.width);add(canvas.height);addNumber(canvas.opacity);}
    section("sketches",document.sketches.size());for(const auto& sketch:document.sketches){add(sketch.name);add(sketchToJson(sketch.sk));}
    section("features",document.features.size());for(const auto& feature:document.features){
        add(std::to_string(feature.id));add(featureTypeName(feature.type));add(feature.name);add(std::to_string(feature.opInt));add(feature.suppressed?"1":"0");add(feature.material);add(feature.manufacturingRoute);
        add(feature.part.dump());add(feature.appearance.dump());add(feature.manufacturingProcess.dump());add(feature.nativeSpec.dump());add(feature.sourceData.dump());addMap(feature.p);
        section("inputs",feature.inputs.size());for(auto input:feature.inputs)add(std::to_string(input));
        section("bends",feature.bends.size());for(const auto& bend:feature.bends){add(bend.feed);add(bend.radius);add(bend.angle);add(bend.roll);}
        section("notches",feature.notches.size());for(const auto& notch:feature.notches){add(notch.station);add(notch.angle);add(notch.face);}
        section("face_appearances",feature.faceAppearances.size());for(const auto& face:feature.faceAppearances)add(faceAppearanceToJson(face).dump());
    }
    section("assembly",document.assembly.size());for(const auto& node:document.assembly){add(std::to_string(node.id));add(std::to_string(node.parent));add(std::to_string(node.feature));add(node.name);add(node.visible?"1":"0");add(node.fixed?"1":"0");addMap(node.p);add(node.pattern.active?assemblyPatternToJson(node.pattern).dump():std::string());}
    section("mates",document.mates.size());for(const auto& mate:document.mates)add(mateToJson(mate).dump());
    return iterationSha256(stamp);
}
V projectContextGraph(const Document& document,u64 revision,const V& args,V& cache,std::string& cachedHash,std::string& cachedStamp){
    for(const auto& item:args.obj)if(item.first!="query"&&item.first!="keys"&&item.first!="depth"&&item.first!="limit"&&item.first!="expected_revision"&&item.first!="known_fingerprints"&&item.first!="known_scope"&&item.first!="history_limit")throw std::runtime_error("Unknown context_graph argument: "+item.first);
    if(const auto* expected=args.find("expected_revision"))if(expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; retrieve current context_graph");
    const int depth=contextInteger(args,"depth",1,0,2),limit=contextInteger(args,"limit",12,1,64),historyLimit=contextInteger(args,"history_limit",3,0,8);
    // An empty query is treated as absent, as in object_context.
    const auto* queryArg=args.find("query");const auto query=queryArg&&!(queryArg->t==V::Str&&queryArg->str.empty())?str(args,"query"):std::string();if(query.size()>256)throw std::runtime_error("context_graph query exceeds 256 bytes");
    if(args.find("keys")&&!query.empty())throw std::runtime_error("Use context_graph keys or query, not both");
    std::string usageError;if(!validateUsageLedger(document.usage,usageError))throw std::runtime_error(usageError);validateAppearanceAuthoring024B(document,false);
    const auto stamp=authoringStamp(document);std::string hash,scope;bool reused=false;
    auto bindScope=[&](const std::string& sourceHash){if(!document.bomDocumentId.empty())return document.bomDocumentId;if(document.agentActivity&&!document.agentActivity->id.empty())return document.agentActivity->id;return std::string("unassigned:")+sourceHash;};
    if(stamp==cachedStamp&&!cache.isNull()){hash=cachedHash;scope=bindScope(hash);reused=graphText(cache,"scope")==scope;}
    if(!reused){const auto saved=documentToJson(document,false,false);contextGraphAuthoringSerializationsCount.fetch_add(1,std::memory_order_relaxed);hash=iterationSha256(saved);scope=bindScope(hash);const bool same=hash==cachedHash&&!cache.isNull()&&graphText(cache,"scope")==scope;if(!same){V authored;if(!arkjson::parse(saved,authored))throw std::runtime_error("Cannot index project authoring");cache=buildProjectGraph(authored,scope);cachedHash=hash;}cachedStamp=stamp;reused=same;}
    const auto& nodes=*cache.find("nodes");const auto& edges=*cache.find("edges");const auto* known=args.find("known_fingerprints");
    if(known){if(known->t!=V::Obj||known->obj.size()>1024||graphText(args,"known_scope")!=scope)throw std::runtime_error("context_graph known_fingerprints require the matching known_scope");for(const auto& item:known->obj)if(item.second.t!=V::Str||item.second.str.size()>64)throw std::runtime_error("Invalid known graph fingerprint");}
    std::set<std::string> roots;const auto words=graphWords(query);if(!query.empty()&&words.empty())throw std::runtime_error("context_graph query needs a word or an exact key");
    if(const auto* keys=args.find("keys")){if(keys->t!=V::Arr||keys->arr.empty()||keys->arr.size()>64)throw std::runtime_error("context_graph keys needs 1..64 exact graph keys");for(const auto& key:keys->arr){if(key.t!=V::Str||!nodes.find(key.str.c_str()))throw std::runtime_error("Unknown graph key; search context_graph first");if(!roots.insert(key.str).second)throw std::runtime_error("Graph keys must be distinct");}}
    else for(const auto& node:nodes.obj){const auto text=contextLower(graphText(node.second,"name")+" "+graphText(node.second,"kind")+" "+node.second.find("facts")->dump());bool match=words.empty();for(const auto& word:words)match|=text.find(word)!=std::string::npos;if(match)roots.insert(node.first);}
    if(args.find("keys")&&roots.size()>(size_t)limit)throw std::runtime_error("Graph limit is smaller than explicit root selection");
    std::vector<std::string> order;std::set<std::string> selected;for(const auto& key:roots)if(order.size()<(size_t)limit){order.push_back(key);selected.insert(key);}
    bool partial=roots.size()>order.size();auto frontier=selected;
    for(int level=0;level<depth;++level){std::set<std::string> next;for(const auto& edge:edges.arr){const auto from=graphText(edge,"from"),to=graphText(edge,"to");if(frontier.count(from)&&!selected.count(to))next.insert(to);if(frontier.count(to)&&!selected.count(from))next.insert(from);}frontier.clear();for(const auto& key:next){if(order.size()>=(size_t)limit){partial=true;break;}selected.insert(key);frontier.insert(key);order.push_back(key);}}
    V rows=V::makeArr(),links=V::makeArr(),fingerprints=V::makeObj(),seeds=V::makeArr();for(const auto& key:roots)if(selected.count(key))seeds.arr.push_back(V::makeStr(key));
    for(const auto& key:order){V row=*nodes.find(key.c_str());const auto fingerprint=graphText(row,"fingerprint");fingerprints.set(key,V::makeStr(fingerprint));const auto* old=known?known->find(key.c_str()):nullptr;const bool changed=!old||old->str!=fingerprint;row.set("changed",V::makeBool(changed));if(!changed)row.obj.erase(std::remove_if(row.obj.begin(),row.obj.end(),[](const auto& item){return item.first=="facts";}),row.obj.end());rows.arr.push_back(row);}
    for(const auto& edge:edges.arr)if(selected.count(graphText(edge,"from"))&&selected.count(graphText(edge,"to")))links.arr.push_back(edge);
    V history=V::makeArr();if(historyLimit&&document.agentActivity)for(auto it=document.agentActivity->entries.rbegin();it!=document.agentActivity->entries.rend()&&history.arr.size()<(size_t)historyLimit;++it){const auto* feedback=it->find("feedback"),*review=it->find("bom_review");std::string text=graphText(*it,"error");if(feedback)text+=" "+graphText(*feedback,"action")+" "+graphText(*feedback,"issue")+" "+graphText(*feedback,"wished_tool")+" "+graphText(*feedback,"wished_options");if(review)text+=" "+graphText(*review,"pending_notes");bool match=query.empty()&&review;for(const auto& word:words)match|=contextLower(text).find(word)!=std::string::npos;if(!match)continue;V note=V::makeObj();for(const char* field:{"id","utc","tool","is_error","revision_after","error_code"})if(const auto* value=it->find(field))note.set(field,*value);if(const auto* error=it->find("error");error&&error->t==V::Str)note.set("error",V::makeStr(graphClip(error->str)));if(const auto* actor=it->find("actor"))note.set("actor",contextCompact(*actor));if(feedback){note.set("feedback_provenance",V::makeStr(graphText(*feedback,"provenance")));note.set("action",V::makeStr(graphClip(graphText(*feedback,"action"))));note.set("issue",V::makeStr(graphClip(graphText(*feedback,"issue"))));for(const char* field:{"wished_tool","wished_options"})if(!graphText(*feedback,field).empty())note.set(field,V::makeStr(graphClip(graphText(*feedback,field))));}if(review){note.set("pending_notes",V::makeStr(graphClip(graphText(*review,"pending_notes"),1024)));note.set("matches_current_authoring",V::makeBool(graphText(*review,"authoring_sha256")==hash));}note.set("authority",V::makeStr("historical host receipt and attributed agent statements; re-inspect current geometry"));history.arr.push_back(note);}
    V out=V::makeObj();out.set("schema",V::makeStr("ark.context-graph.v1"));out.set("scope",V::makeStr(scope));out.set("revision",V::makeNum((double)revision));out.set("source_sha256",V::makeStr(hash));out.set("index_reused",V::makeBool(reused));out.set("total_nodes",V::makeNum(nodes.obj.size()));out.set("total_edges",V::makeNum(edges.arr.size()));out.set("roots",seeds);out.set("nodes",rows);out.set("edges",links);out.set("fingerprints",fingerprints);out.set("history",history);out.set("history_capture_gap",V::makeNum(document.agentActivity?document.agentActivity->unrecordedActions:0));out.set("partial",V::makeBool(partial));out.set("authority",V::makeStr("read-only navigation over saved authoring; links are authored selectors or expression identifiers, not complete geometric dependency proof; use object_context/native inspection before editing"));out.set("persistence",V::makeStr("derived from existing saved authoring and activity; rebuilt after reopen; no document format change"));out.set("manufacturing_qualified",V::makeBool(false));return out;
}
}
