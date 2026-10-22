// Included by agent.cpp: bounded read-only CAD context, no geometry evaluation.
// Fingerprints are document-scoped change tokens, never object identity, a
// security digest, or proof of equal/manufacturable geometry.
namespace {
V contextCanonical(const V& value){
    V sorted=value;
    if(sorted.t==V::Obj){std::sort(sorted.obj.begin(),sorted.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});for(auto& entry:sorted.obj)entry.second=contextCanonical(entry.second);}
    else if(sorted.t==V::Arr)for(auto& entry:sorted.arr)entry=contextCanonical(entry);
    return sorted;
}
std::string contextFingerprint(const V& value){
    u64 digest=14695981039346656037ull;
    for(unsigned char byte:contextCanonical(value).dump()){digest^=byte;digest*=1099511628211ull;}
    char hex[17];std::snprintf(hex,sizeof hex,"%016llx",(unsigned long long)digest);
    return std::string("oc1-fnv64:")+hex;
}
V contextOmitted(const V& value,const char* reason){V report=V::makeObj();report.set("omitted",V::makeBool(true));report.set("reason",V::makeStr(reason));report.set("json_bytes",V::makeNum(value.dump().size()));report.set("fingerprint",V::makeStr(contextFingerprint(value)));return report;}
V contextCompact(const V& value,size_t depth=0){
    if(depth>10||value.dump().size()>8192)return contextOmitted(value,"bounded_context; use the corresponding inspection tool");
    if(value.t==V::Str&&value.str.size()>1024)return contextOmitted(value,"large authored string");
    if(value.t==V::Arr&&value.arr.size()>64)return contextOmitted(value,"large authored array");
    V compact=value;
    if(compact.t==V::Obj)for(auto& item:compact.obj){
        if((item.first=="source"&&(item.second.t==V::Obj||item.second.t==V::Arr))||item.first=="mesh"||item.first=="brep"||item.first=="brep_base64"||item.first=="step_base64"||item.first=="positions"||item.first=="indices")item.second=contextOmitted(item.second,"geometry/source buffer retained locally");
        else item.second=contextCompact(item.second,depth+1);
    }else if(compact.t==V::Arr)for(auto& item:compact.arr)item=contextCompact(item,depth+1);
    return compact;
}
int contextInteger(const V& args,const char* key,int fallback,int low,int high){
    const auto* value=args.find(key);if(!value)return fallback;
    if(value->t!=V::Num||!std::isfinite(value->num)||value->num<low||value->num>high||value->num!=std::floor(value->num))throw std::runtime_error(std::string("object_context ")+key+" is outside its integer bounds");
    return (int)value->num;
}
std::string contextKey(u64 feature){return "feature:"+std::to_string(feature);}
std::string occurrenceKey(u64 node,int copy){return "occurrence:"+std::to_string(node)+":"+std::to_string(copy);}
std::string contextLower(std::string value){for(char& c:value)c=(char)std::tolower((unsigned char)c);return value;}
struct ObjectContext {
    const Document& document;
    V saved,design;
    std::map<u64,V> features,measured,nodes;
    std::map<std::string,V> resources;
    std::vector<AssemblyPlacement> placements;
    std::string assemblyError;
    explicit ObjectContext(const Document& doc,u64 revision):document(doc),design(designView(doc,revision)){
        if(!arkjson::parse(documentToJson(doc,false,false),saved))throw std::runtime_error("Could not read authored context");
        for(const auto& row:saved.find("features")->arr)features[(u64)row.find("id")->num]=row;
        for(const auto& row:design.find("features")->arr)measured[(u64)row.find("id")->num]=row;
        if(const auto* rows=saved.find("assembly"))for(const auto& row:rows->arr)nodes[(u64)row.find("id")->num]=row;
        for(const char* section:{"params","sketches","planes","materials"})if(const auto* rows=saved.find(section))for(const auto& row:rows->arr)resources[std::string(section)+":"+row.find("name")->str]=row;
        if(!doc.assembly.empty())resolveAssembly(doc,placements,assemblyError,nullptr,true);
    }
    // Follow explicit source references and expression identifiers. Recursion
    // through resources captures parameter cascades, datum supports and sketch
    // projections without treating tessellation indices as persistent references.
    void scan(const V& value,std::set<u64>& ids,std::set<std::string>& names,std::set<u64>& occurrences,const std::string& key="")const{
        if(key=="owner_feature"||key=="local_name")return; // Scope metadata is not a geometric source.
        if(value.t==V::Str){
            for(const char* section:{"sketches","planes","materials"}){const std::string candidate=std::string(section)+":"+value.str;if(resources.count(candidate))names.insert(candidate);}
            if(key!="name"&&key!="material"&&key!="reference")for(const auto& feature:document.features)if(!feature.name.empty()&&feature.name==value.str)ids.insert(feature.id);
            for(size_t i=0;i<value.str.size();){if(std::isalpha((unsigned char)value.str[i])||value.str[i]=='_'){size_t start=i++;while(i<value.str.size()&&(std::isalnum((unsigned char)value.str[i])||value.str[i]=='_'))++i;const std::string candidate="params:"+value.str.substr(start,i-start);if(resources.count(candidate))names.insert(candidate);}else ++i;}
            if(key=="instances")for(const auto& node:document.assembly)if(node.name==value.str)occurrences.insert(node.id);
        }else if(value.t==V::Num){
            if((key=="feature"||key=="source_feature"||key=="second_feature")&&features.count((u64)value.num))ids.insert((u64)value.num);
            if((key=="source_instance"||key=="instances")&&nodes.count((u64)value.num))occurrences.insert((u64)value.num);
        }else if(value.t==V::Obj){for(const auto& entry:value.obj)if(entry.first!="source"&&entry.first!="mesh")scan(entry.second,ids,names,occurrences,entry.first);}
        else if(value.t==V::Arr)for(const auto& entry:value.arr)scan(entry,ids,names,occurrences,key);
    }
    std::set<u64> dependencies(u64 feature)const{
        std::set<u64> ids,occurrences;std::set<std::string> names;scan(features.at(feature),ids,names,occurrences);
        std::set<std::string> read;while(read.size()!=names.size()){const auto pending=names;for(const auto& name:pending)if(read.insert(name).second)scan(resources.at(name),ids,names,occurrences);}
        for(const auto& input:features.at(feature).find("inputs")->arr)ids.insert((u64)input.num);
        for(u64 node:occurrences){if(nodes.at(node).find("feature")->num>0)ids.insert((u64)nodes.at(node).find("feature")->num);for(const auto& place:placements)if(place.node==node)ids.insert(place.feature);}
        ids.erase(feature);return ids;
    }
    const AssemblyPlacement& placement(u64 node,int copy)const{
        if(!assemblyError.empty())throw std::runtime_error("OBJECT_CONTEXT_REFERENCE_MISSING: assembly placement unavailable: "+assemblyError);
        for(const auto& place:placements)if(place.node==node&&place.copy==copy)return place;
        throw std::runtime_error("OBJECT_CONTEXT_REFERENCE_MISSING: occurrence copy not found");
    }
    V occurrence(u64 node,int copy)const{
        const auto& place=placement(node,copy);
        V row=V::makeObj();row.set("key",V::makeStr(occurrenceKey(node,copy)));row.set("node",V::makeNum((double)node));row.set("copy",V::makeNum(copy));row.set("authored",nodes.at(node));
        row.set("feature",V::makeNum((double)place.feature));V matrix=V::makeArr();for(double value:place.world.m)matrix.arr.push_back(V::makeNum(value));row.set("world_column_major",matrix);row.set("visible",V::makeBool(place.visible));return row;
    }
    V state(u64 feature,u64 selectedNode=0,int copy=0)const{
        V state=V::makeObj(),authors=V::makeObj(),evidence=V::makeObj(),linked=V::makeObj(),assembly=V::makeObj(),mates=V::makeArr();
        std::set<u64> ids{feature},visited,occurrences;std::set<std::string> names,read;
        if(selectedNode)occurrences.insert(selectedNode);
        else for(const auto& place:placements)if(place.feature==feature)occurrences.insert(place.node);
        bool changed=true;
        while(changed){changed=false;
            const auto pending=ids;for(u64 current:pending)if(visited.insert(current).second){changed=true;const auto& authored=features.at(current);for(const auto& sketch:document.sketches)if(sketch.ownerFeature==current)names.insert("sketches:"+sketch.name);authors.set(contextKey(current),authored);evidence.set(contextKey(current),measured.at(current));scan(authored,ids,names,occurrences);for(u64 dependency:dependencies(current))ids.insert(dependency);}
            const auto pendingNames=names;for(const auto& name:pendingNames)if(read.insert(name).second){changed=true;const auto& resource=resources.at(name);linked.set(name,resource);scan(resource,ids,names,occurrences);}
            // A mate can couple this occurrence to a remotely authored part.
            // Keep its whole connected neighborhood in the change token even
            // when only direct neighbors are exposed in the bounded payload.
            const auto pendingNodes=occurrences;for(u64 node:pendingNodes){const auto& authored=nodes.at(node);const u64 parent=(u64)authored.find("parent")->num;if(parent&&occurrences.insert(parent).second)changed=true;const u64 definition=(u64)authored.find("feature")->num;if(definition&&ids.insert(definition).second)changed=true;for(const auto& place:placements)if(place.node==node&&ids.insert(place.feature).second)changed=true;scan(authored,ids,names,occurrences);}
            for(const auto& mate:document.mates)if(occurrences.count(mate.a)||occurrences.count(mate.b)){if(occurrences.insert(mate.a).second)changed=true;if(occurrences.insert(mate.b).second)changed=true;scan(mateToJson(mate),ids,names,occurrences);}
            // scan may discover extra ids/resources after their pass.
            changed=changed||ids.size()!=visited.size()||names.size()!=read.size()||occurrences.size()!=pendingNodes.size();
        }
        for(u64 node:occurrences){assembly.set(std::to_string(node),nodes.at(node));V poses=V::makeArr();for(const auto& placement:placements)if(placement.node==node){V matrix=V::makeArr();for(double value:placement.world.m)matrix.arr.push_back(V::makeNum(value));poses.arr.push_back(matrix);}assembly.set("poses:"+std::to_string(node),poses);}
        for(const auto& mate:document.mates)if(occurrences.count(mate.a)||occurrences.count(mate.b))mates.arr.push_back(mateToJson(mate));
        state.set("authored",authors);state.set("measured",evidence);state.set("resources",linked);state.set("assembly",assembly);state.set("mates",mates);state.set("assembly_error",V::makeStr(assemblyError));
        if(selectedNode)state.set("occurrence",occurrence(selectedNode,copy));return state;
    }
    V row(u64 feature,u64 node,int copy,const V* known)const{
        const std::string key=node?occurrenceKey(node,copy):contextKey(feature),fingerprint=contextFingerprint(state(feature,node,copy));
        V row=V::makeObj();row.set("key",V::makeStr(key));row.set("id",V::makeNum((double)feature));row.set("name",*features.at(feature).find("name"));row.set("type",*features.at(feature).find("type"));row.set("fingerprint",V::makeStr(fingerprint));
        const auto* previous=known?known->find(key.c_str()):nullptr;const bool changed=!previous||previous->str!=fingerprint;row.set("changed",V::makeBool(changed));
        if(node){row.set("node",V::makeNum((double)node));row.set("copy",V::makeNum(copy));}
        if(!changed)return row;
        row.set("authored",contextCompact(features.at(feature)));
        V measurements=V::makeObj();for(const char* key:{"ok","error","volume_mm3","bounds_mm","density_g_cm3"})if(const auto* value=measured.at(feature).find(key))measurements.set(key,*value);
        measurements.set("bounds_authority",V::makeStr("display_geometry"));measurements.set("volume_authority",V::makeStr("unavailable"));
        for(const auto& body:document.evalBodies)if(body.id==feature)if(const auto* report=body.nativeData.find("report")){if(report->find("bounds_mm"))measurements.set("bounds_authority",V::makeStr("native_brep_report"));if(report->find("volume_mm3"))measurements.set("volume_authority",V::makeStr("native_brep_report"));}
        measurements.set("manufacturing_qualified",V::makeBool(false));row.set("measured",measurements);
        V references=V::makeObj(),deps=V::makeArr(),linked=V::makeObj(),neighborhood=V::makeArr(),mates=V::makeArr();
        for(u64 dependency:dependencies(feature))deps.arr.push_back(V::makeStr(contextKey(dependency)));references.set("dependencies",deps);
        std::set<u64> ids,occurrences;std::set<std::string> names;scan(features.at(feature),ids,names,occurrences);
        // Surface linked authoring recipes; transitive resources remain covered
        // by the fingerprint and can be inspected separately.
        for(const auto& name:names)linked.set(name,contextCompact(resources.at(name)));references.set("resources",linked);
        std::set<u64> direct;
        if(node)direct.insert(node);else for(const auto& place:placements)if(place.feature==feature)direct.insert(place.node);
        for(u64 selected:direct){const u64 parent=(u64)nodes.at(selected).find("parent")->num;if(parent)occurrences.insert(parent);for(const auto& mate:document.mates)if(mate.a==selected||mate.b==selected){mates.arr.push_back(contextCompact(mateToJson(mate)));occurrences.insert(mate.a==selected?mate.b:mate.a);}}
        for(u64 adjacent:occurrences){V neighbor=V::makeObj();neighbor.set("node",V::makeNum((double)adjacent));neighbor.set("name",*nodes.at(adjacent).find("name"));neighbor.set("feature",*nodes.at(adjacent).find("feature"));neighbor.set("parent",*nodes.at(adjacent).find("parent"));neighborhood.arr.push_back(neighbor);}
        references.set("assembly_neighbors",contextCompact(neighborhood));references.set("mates",contextCompact(mates));row.set("references",references);
        V placed=V::makeArr();if(node)placed.arr.push_back(occurrence(node,copy));else for(const auto& place:placements)if(place.feature==feature)placed.arr.push_back(occurrence(place.node,place.copy));
        row.set("occurrences",contextCompact(placed));return row;
    }
};
V objectContext(const Document& document,u64 revision,const V& args){
    // An empty query selects nothing extra; agents send query:"" beside ids.
    if(const auto* query=args.find("query");query&&query->t==V::Str&&query->str.empty()){V normalized=args;normalized.obj.erase(std::remove_if(normalized.obj.begin(),normalized.obj.end(),[](const auto& entry){return entry.first=="query";}),normalized.obj.end());return objectContext(document,revision,normalized);}
    for(const auto& field:args.obj)if(field.first!="ids"&&field.first!="node"&&field.first!="copy"&&field.first!="query"&&field.first!="type"&&field.first!="expected_revision"&&field.first!="known_fingerprints"&&field.first!="depth"&&field.first!="limit"&&field.first!="after_id")throw std::runtime_error("Unknown object_context field: "+field.first);
    if(const auto* expected=args.find("expected_revision"))if(expected->t!=V::Num||expected->num!=(double)revision)throw std::runtime_error("Revision conflict; refresh object_context before editing");
    const int depth=contextInteger(args,"depth",0,0,2),limit=contextInteger(args,"limit",16,1,64),copy=contextInteger(args,"copy",0,0,31);
    const auto* known=args.find("known_fingerprints");if(known){if(known->t!=V::Obj||known->obj.size()>1024)throw std::runtime_error("object_context known_fingerprints must be an object of at most 1024 keys");for(const auto& value:known->obj)if(value.second.t!=V::Str||value.second.str.size()>64||value.first.size()>64)throw std::runtime_error("object_context fingerprint entries must be bounded strings");}
    if(args.find("copy")&&!args.find("node"))throw std::runtime_error("object_context copy requires node");
    if(args.find("node")&&(args.find("ids")||args.find("query")||args.find("type")||args.find("after_id")))throw std::runtime_error("object_context node cannot be combined with feature selectors");
    if(args.find("ids")&&(args.find("query")||args.find("type")||args.find("after_id")))throw std::runtime_error("object_context ids cannot be combined with discovery selectors");
    const std::string query=args.find("query")?contextLower(str(args,"query")):"",type=args.find("type")?str(args,"type"):"";
    if(query.size()>128)throw std::runtime_error("object_context query exceeds 128 characters");if(!type.empty()){FeatureType checked;if(!featureTypeFromName(type,checked)){std::set<std::string> valid;for(const auto& feature:document.features)valid.insert(featureTypeName(feature.type));std::string available;for(const auto& name:valid){if(!available.empty())available+=", ";available+=name;}throw std::runtime_error("object_context type '"+type+"' is unknown; valid types present in this document: "+(available.empty()?std::string("none; discover with query or ids"):available));}}
    ObjectContext context(document,revision);u64 selectedNode=0;std::set<u64> selection;
    if(const auto* selected=args.find("node")){selectedNode=nodeRef(document,*selected);const auto found=std::find_if(document.assembly.begin(),document.assembly.end(),[&](const AssemblyNode& node){return node.id==selectedNode;});if(found==document.assembly.end())throw std::runtime_error("OBJECT_CONTEXT_REFERENCE_MISSING: assembly occurrence not found");if(!found->feature)throw std::runtime_error("object_context node must select a part occurrence");const auto& place=context.placement(selectedNode,copy);selection.insert(place.feature);}
    else if(const auto* ids=args.find("ids")){if(ids->t!=V::Arr||ids->arr.empty()||ids->arr.size()>64)throw std::runtime_error("object_context ids requires 1..64 feature references");for(const auto& requested:ids->arr){const u64 feature=featureRef(document,requested);if(!document.find(feature))throw std::runtime_error("OBJECT_CONTEXT_REFERENCE_MISSING: feature not found");if(!selection.insert(feature).second)throw std::runtime_error("object_context ids must be distinct");}if(selection.size()>(size_t)limit)throw std::runtime_error("object_context limit is smaller than the explicit selection");}
    else {const u64 after=args.find("after_id")?id(*args.find("after_id")):0;for(const auto& feature:document.features)if(feature.id>after&&(query.empty()||contextLower(feature.name).find(query)!=std::string::npos)&&(type.empty()||type==featureTypeName(feature.type)))selection.insert(feature.id);}
    const std::set<u64> roots=selection;
    bool truncated=false;u64 next=0;V rows=V::makeArr(),fingerprints=V::makeObj(),selected=V::makeArr();size_t bytes=0;
    for(u64 feature:roots)selected.arr.push_back(V::makeNum((double)feature));
    if(depth){auto frontier=selection;for(int level=0;level<depth;++level){std::set<u64> nextLevel;for(u64 feature:frontier){for(u64 dependency:context.dependencies(feature))nextLevel.insert(dependency);for(const auto& other:document.features)if(context.dependencies(other.id).count(feature))nextLevel.insert(other.id);}selection.insert(nextLevel.begin(),nextLevel.end());frontier=std::move(nextLevel);}}
    std::vector<u64> ordered(roots.begin(),roots.end());for(u64 feature:selection)if(!roots.count(feature))ordered.push_back(feature);
    for(u64 feature:ordered){if(rows.arr.size()>=(size_t)limit){truncated=true;break;}V row=context.row(feature,selectedNode&&roots.count(feature)?selectedNode:0,copy,known);if(row.dump().size()>24576){V kept=V::makeObj();for(const char* key:{"key","id","name","type","fingerprint","changed","node","copy"})if(const auto* value=row.find(key))kept.set(key,*value);kept.set("details",contextOmitted(row,"row exceeds 24 KiB context bound; inspect selected feature"));row=std::move(kept);}bytes+=row.dump().size();if(bytes>262144){truncated=true;break;}fingerprints.set(row.find("key")->str,*row.find("fingerprint"));rows.arr.push_back(std::move(row));next=feature;}
    V result=V::makeObj();result.set("schema",V::makeStr("ark-object-context-v1"));result.set("revision",V::makeNum((double)revision));result.set("units",V::makeStr("mm"));result.set("identity_scope",V::makeStr("persisted IDs within the current document; copies belong to their pattern node"));result.set("fingerprint_contract",V::makeStr("canonical authored/dependency/resource/assembly and available measured context; change detection only, not identity, security, geometric equivalence or manufacturing qualification"));result.set("selected_ids",contextCompact(selected));result.set("objects",rows);result.set("fingerprints",fingerprints);result.set("partial",V::makeBool(true));result.set("truncated",V::makeBool(truncated));result.set("matched_count",V::makeNum(roots.size()));result.set("has_more",V::makeBool(truncated));if(truncated&&!args.find("ids")&&!selectedNode&&!depth)result.set("next_after_id",V::makeNum((double)next));
    // Paging every detailed row of a large design grows the history past its
    // projection bound, so earlier pages are compacted and re-read in a loop.
    // Discovery therefore lists every match compactly; details come from ids.
    if(truncated&&!args.find("ids")&&!selectedNode){V index=V::makeArr();for(u64 feature:roots){if(index.arr.size()>=512)break;const auto* found=document.find(feature);if(!found)continue;V entry=V::makeObj();entry.set("id",V::makeNum((double)feature));entry.set("name",V::makeStr(found->name));entry.set("type",V::makeStr(featureTypeName(found->type)));index.arr.push_back(std::move(entry));}
        result.set("index",index);if(roots.size()>index.arr.size())result.set("index_truncated",V::makeBool(true));
        result.set("next_action",V::makeStr("index lists every match by id, name and type. Request details only for the objects your edit depends on with ids:[...] (up to 64); paging through every detailed row is rarely needed."));}
    // known_fingerprints omits unchanged details; after history compaction the
    // earlier detailed receipt may be gone, so say how to get the details back.
    std::size_t unchanged=0;for(const auto& row:rows.arr)if(const auto* changed=row.find("changed");changed&&changed->t==V::Bool&&!changed->b)++unchanged;
    if(unchanged){result.set("unchanged_rows_without_details",V::makeNum((double)unchanged));result.set("unchanged_details_note",V::makeStr("Rows whose fingerprint matched known_fingerprints omit their details. If those details are no longer in your context, request the same ids without known_fingerprints."));}
    result.set("document_unchanged",V::makeBool(true));result.set("evidence_limits",V::makeStr("authored recipes are declarations; measured volume is native only when available; other bounds may come from display geometry. No meshes, images or B-rep buffers; use native/reference/physics inspections for their qualified evidence."));return result;
}
}
