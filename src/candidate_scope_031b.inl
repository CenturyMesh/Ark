// Included inside agent_transactions.cpp's anonymous namespace. The scope is
// deliberately conservative: unsupported recipes still preview normally, but
// require a new explicit recipe preview after another commit.
bool refreshFeatureSupported(const Feature& feature){
    std::function<bool(const V&)> retainedOnly=[&](const V& value){for(const auto& field:value.obj){if(field.first=="instances"||field.first=="node"||field.first=="source_instance"||field.first=="alternate_node"||field.first=="source_sketch")return false;if(!retainedOnly(field.second))return false;}for(const auto& item:value.arr)if(!retainedOnly(item))return false;return true;};
    if(!retainedOnly(feature.nativeSpec))return false;
    switch(feature.type){
    case FeatureType::Box:case FeatureType::Cylinder:case FeatureType::Sphere:
    case FeatureType::Cone:case FeatureType::Torus:case FeatureType::Extrude:
    case FeatureType::Revolve:return true;
    // Named-geometry consumers: their sketches, planes and parameters are read by
    // name, so the complete authored context and every feature stay in scope.
    case FeatureType::Loft:case FeatureType::RevolveSurface:case FeatureType::ExtrudeSurface:
    case FeatureType::Sweep:return true;
    case FeatureType::Thicken:case FeatureType::SketchCut:return !feature.inputs.empty();
    case FeatureType::Transform:case FeatureType::Boolean:case FeatureType::LinearPattern:
        return !feature.inputs.empty();
    case FeatureType::NativeSolid:{
        if(feature.nativeSpec.find("instances"))return false;
        std::string op=feature.get("op");if(op.empty())if(const auto* value=feature.nativeSpec.find("op"))op=value->asStr();
        static const std::set<std::string> operations{"box","sphere","cylinder","cone","torus","extrude","union","cut","common","transform","linear_pattern","circular_pattern","mirror","fillet","chamfer","shell","draft","split_plane","hole"};
        return operations.count(op)!=0;
    }
    default:return false;
    }
}
V candidateReadScope(const Document& document,const V& recipe){
    const auto* commands=recipe.find("commands");if(!commands||commands->t!=V::Arr)throw std::runtime_error("missing commands");
    V authored;if(!arkjson::parse(documentToJson(document,false,false),authored))throw std::runtime_error("authoring unavailable");
    const V features=*authored.find("features");
    // Allocation counters are not read: the recipe may only name nodes and
    // features that existed at preview time or that it adds itself.
    authored.obj.erase(std::remove_if(authored.obj.begin(),authored.obj.end(),[](const auto& field){return field.first=="features"||field.first=="next_feature_id"||field.first=="next_assembly_id"||field.first=="version";}),authored.obj.end());
    V bindings=V::makeArr();std::set<u64> reads,editedTargets;std::set<std::string> localNames,localNodes;bool readsGeometricContext=false;
    auto resolve=[&](const V& selector)->u64{
        const Feature* selected=nullptr;
        if(selector.t==V::Num){if(!std::isfinite(selector.num)||selector.num<1||selector.num!=std::floor(selector.num)||selector.num>9007199254740990.)throw std::runtime_error("invalid numeric feature selector");selected=document.find((u64)selector.num);}
        else if(selector.t==V::Str){for(const auto& f:document.features)if(f.name==selector.str){if(selected)throw std::runtime_error("ambiguous feature name: "+selector.str);selected=&f;}}
        else throw std::runtime_error("unsupported feature selector");
        if(!selected)throw std::runtime_error("missing feature or provisional numeric ID; use retained existing IDs or earlier unique added names");
        V binding=V::makeObj();binding.set("selector",selector);binding.set("id",V::makeNum((double)selected->id));bindings.arr.push_back(std::move(binding));return selected->id;
    };
    auto geometricContext=[](const Feature& feature){return feature.type==FeatureType::Extrude||feature.type==FeatureType::Revolve||feature.type==FeatureType::Loft||feature.type==FeatureType::RevolveSurface||feature.type==FeatureType::ExtrudeSurface||feature.type==FeatureType::Sweep||feature.type==FeatureType::SketchCut||!feature.get("sketch").empty()||!feature.get("plane").empty()||feature.nativeSpec.dump().find("\"plane\"")!=std::string::npos;};
    std::function<void(u64)> include=[&](u64 id){if(!reads.insert(id).second)return;const auto* feature=document.find(id);if(!feature||!refreshFeatureSupported(*feature))throw std::runtime_error("feature dependency uses an unaudited or implicit read scope");readsGeometricContext|=geometricContext(*feature);for(auto input:feature->inputs)include(input);};
    auto input=[&](const V& selector){if(selector.t==V::Str&&localNames.count(selector.str))return;include(resolve(selector));};
    // New named definitions authored by the recipe itself. Editing an existing
    // definition still needs a fresh preview: its old value is not in scope.
    static const std::map<std::string,std::string> definitionCollections{{"sketch.set","sketches"},{"plane.set","planes"},{"param.set","params"}};
    std::set<std::string> localDefinitions;
    for(const auto& command:commands->arr){
        if(command.find("p_remove"))throw std::runtime_error("parameter removal requires a fresh explicit preview");
        if(const auto* operation=command.find("op");operation&&operation->t==V::Str)if(const auto collection=definitionCollections.find(operation->str);collection!=definitionCollections.end()){
            const auto* name=command.find("name");if(!name||name->t!=V::Str||name->str.empty())throw std::runtime_error("definition name is missing");
            if(!localDefinitions.insert(collection->second+"/"+name->str).second)throw std::runtime_error("repeated definitions of one name require a fresh explicit preview");
            if(const auto* rows=authored.find(collection->second.c_str());rows&&rows->t==V::Arr)for(const auto& row:rows->arr)if(const auto* existing=row.find("name");existing&&existing->t==V::Str&&existing->str==name->str)throw std::runtime_error("editing existing "+collection->second+" entry "+name->str+" requires a fresh explicit preview");
            if(operation->str!="param.set")readsGeometricContext=true;
            continue;
        }
        // A new instance reads its part, its parent and the placement parameters
        // (all in scope). A provisional numeric node ID would rebind to a peer's
        // node after another commit, so later references use the local name.
        if(const auto* operation=command.find("op");operation&&operation->t==V::Str&&operation->str=="assembly.add"){
            for(const auto& field:command.obj)if(field.first!="op"&&field.first!="name"&&field.first!="parent"&&field.first!="feature"&&field.first!="p"&&field.first!="visible"&&field.first!="fixed"&&field.first!="pattern")throw std::runtime_error("assembly.add field "+field.first+" requires a fresh explicit preview");
            const auto* name=command.find("name");if(!name||name->t!=V::Str||name->str.empty()||!localNodes.insert(name->str).second)throw std::runtime_error("added assembly nodes need unique names");
            for(const auto& node:document.assembly)if(node.name==name->str)throw std::runtime_error("added assembly node name already exists: "+name->str);
            if(const auto* parent=command.find("parent")){
                if(parent->t==V::Num){if(parent->num!=0&&std::none_of(document.assembly.begin(),document.assembly.end(),[&](const AssemblyNode& node){return (double)node.id==parent->num;}))throw std::runtime_error("provisional numeric assembly parent; use the parent's name");}
                else if(parent->t!=V::Str||(!localNodes.count(parent->str)&&std::count_if(document.assembly.begin(),document.assembly.end(),[&](const AssemblyNode& node){return node.name==parent->str;})!=1))throw std::runtime_error("assembly parent must be an existing node or an earlier added name");
            }
            if(const auto* feature=command.find("feature");feature&&!(feature->t==V::Num&&feature->num==0))input(*feature);
            if(const auto* pattern=command.find("pattern");pattern&&pattern->find("source"))throw std::runtime_error("driven instance patterns require a fresh explicit preview");
            continue;
        }
        const auto* operation=command.find("op");if(!operation||operation->t!=V::Str||(operation->str!="feature.add"&&operation->str!="feature.edit"))throw std::runtime_error("only feature.add, feature.edit, assembly.add and new sketch/plane/param definitions have refresh scopes; preview other operations in their own batch");
        Feature effective;bool addition=operation->str=="feature.add";
        if(addition){const auto* type=command.find("type");if(!type||type->t!=V::Str||!featureTypeFromName(type->str,effective.type))throw std::runtime_error("unsupported added feature type");}
        else {const auto* selector=command.find("id");if(!selector)throw std::runtime_error("missing edit target");if(selector->t==V::Str&&localNames.count(selector->str))throw std::runtime_error("editing a provisional feature requires a fresh explicit preview");const auto id=resolve(*selector);if(!editedTargets.insert(id).second)throw std::runtime_error("repeated edits of one feature require a fresh explicit preview");include(id);effective=*document.find(id);}
        if(const auto* values=command.find("p")){if(values->t!=V::Obj)throw std::runtime_error("invalid feature parameters");for(const auto& field:values->obj)effective.p[field.first]=field.second.t==V::Str?field.second.str:field.second.dump();}
        if(const auto* native=command.find("native"))effective.nativeSpec=*native;
        if(const auto* inputs=command.find("inputs")){if(inputs->t!=V::Arr)throw std::runtime_error("invalid feature inputs");effective.inputs.clear();for(const auto& selector:inputs->arr){input(selector);effective.inputs.push_back(1);}}
        if(!refreshFeatureSupported(effective))throw std::runtime_error("feature recipe uses an unaudited or implicit read scope");
        readsGeometricContext|=geometricContext(effective);
        if(!addition)if(const auto* name=command.find("name")){if(name->t!=V::Str)throw std::runtime_error("invalid edited name");for(const auto& feature:document.features)if(feature.name==name->str&&feature.id!=effective.id)throw std::runtime_error("edited feature name collides with another feature");}
        if(addition)if(const auto* name=command.find("name")){if(name->t!=V::Str||name->str.empty()||localNames.count(name->str))throw std::runtime_error("added names must be unique");for(const auto& feature:document.features)if(feature.name==name->str)throw std::runtime_error("added feature name already exists: "+name->str);localNames.insert(name->str);}
    }
    // Re-run supported measured checks on the refreshed candidate. Existing
    // checked definitions join the same read closure; provisional numeric IDs
    // and implicit occurrence/external selectors remain unsupported.
    if(const auto* checks=recipe.find("checks")){
        if(checks->t!=V::Arr)throw std::runtime_error("checks require a fresh explicit preview");
        for(const auto& check:checks->arr){
            const auto* query=check.find("query");const auto* tool=query?query->find("tool"):nullptr;const auto* args=query?query->find("args"):nullptr;
            if(!tool||tool->t!=V::Str||!args||args->t!=V::Obj)throw std::runtime_error("checks require a fresh explicit preview");
            if(tool->str=="eval_expr")continue; // Complete parameter context is retained below.
            if(tool->str!="native_inspect"&&tool->str!="mesh_inspect")throw std::runtime_error("checks require a fresh explicit preview; supported refresh checks: native_inspect, mesh_inspect and eval_expr");
            for(const auto& field:args->obj)if(field.first!="id"&&field.first!="detail")throw std::runtime_error("checked selectors require a fresh explicit preview");
            const auto* id=args->find("id");if(!id)throw std::runtime_error("checked feature ID is missing");input(*id);
        }
    }
    // Named sketches/datums have additional selectors (second_feature,
    // alternate_feature, projected supports, occurrence frames). Until their
    // full read graph is shared with the evaluator, retain the complete feature
    // context whenever the target OR an ancestor consumes named geometry.
    V closure=V::makeArr();for(const auto id:reads)closure.arr.push_back(V::makeNum((double)id));
    if(readsGeometricContext)for(const auto& feature:document.features)reads.insert(feature.id);
    V scope=V::makeObj(),selected=V::makeArr();scope.set("closure_ids",closure);
    for(const auto& row:features.arr){const auto* id=row.find("id");if(id&&reads.count((u64)id->num))selected.arr.push_back(row);}
    scope.set("context",authored);scope.set("features",selected);scope.set("bindings",bindings);
    V tessellation=V::makeArr();for(double x:{document.tessOpt.tol,document.tessOpt.maxEdge,document.tessOpt.minAngleDeg,(double)document.tessOpt.maxLevels})tessellation.arr.push_back(V::makeNum(x));scope.set("tessellation",tessellation);return scope;
}

// Peer work that only ADDS definitions or features cannot change anything this
// recipe read: every entry present at preview time must be identical, nothing
// may be removed, and an added name must not occur in any string of the recipe
// or of its dependency closure (so it cannot be what the recipe refers to).
// Everything else still requires a fresh explicit preview; no rebase is inferred.
bool refreshScopeCompatible(const V& previous,const V& current,const V& recipe,std::string& reason){
    std::vector<std::string> mentions;
    std::function<void(const V&)> collect=[&](const V& value){if(value.t==V::Str)mentions.push_back(value.str);for(const auto& field:value.obj)collect(field.second);for(const auto& item:value.arr)collect(item);};
    collect(recipe);
    std::set<double> closure;if(const auto* ids=previous.find("closure_ids");ids&&ids->t==V::Arr)for(const auto& id:ids->arr)closure.insert(id.num);
    if(const auto* rows=previous.find("features");rows&&rows->t==V::Arr)for(const auto& row:rows->arr)if(const auto* id=row.find("id");id&&closure.count(id->num))collect(row);
    auto mentioned=[&](const std::string& name){return name.empty()||std::any_of(mentions.begin(),mentions.end(),[&](const std::string& text){return text.find(name)!=std::string::npos;});};
    auto same=[](const V* a,const V* b){return (a?a->dump():std::string())==(b?b->dump():std::string());};
    for(const char* exact:{"bindings","tessellation","closure_ids"})if(!same(previous.find(exact),current.find(exact))){reason=std::string("recipe ")+exact+" changed";return false;}
    // Rows keyed by identity: every earlier row identical, additions unmentioned.
    auto additionsOnly=[&](const V* before,const V& after,const char* key,const std::string& label)->bool{
        if(after.t!=V::Arr||(before&&before->t!=V::Arr)){reason=label+" changed";return false;}
        std::map<std::string,std::string> earlier;
        if(before)for(const auto& row:before->arr){const auto* identity=row.find(key);if(!identity||!earlier.emplace(identity->dump(),row.dump()).second){reason=label+" has unkeyed or repeated entries";return false;}}
        for(const auto& row:after.arr){const auto* identity=row.find(key);if(!identity){reason=label+" has unkeyed entries";return false;}
            const auto found=earlier.find(identity->dump());
            if(found!=earlier.end()){if(found->second!=row.dump()){reason=label+" entry "+identity->dump()+" changed";return false;}earlier.erase(found);continue;}
            const auto* name=row.find("name");if(mentioned(name&&name->t==V::Str?name->str:std::string())){reason=label+" gained "+(name&&name->t==V::Str?"\""+name->str+"\"":std::string("an unnamed entry"))+", which this recipe may reference";return false;}
        }
        if(!earlier.empty()){reason=label+" entry "+earlier.begin()->first+" was removed";return false;}
        return true;
    };
    const auto* beforeFeatures=previous.find("features");const auto* afterFeatures=current.find("features");
    if(!afterFeatures||!additionsOnly(beforeFeatures,*afterFeatures,"id","features"))return false;
    const auto* beforeContext=previous.find("context");const auto* afterContext=current.find("context");
    if(!beforeContext||!afterContext||beforeContext->t!=V::Obj||afterContext->t!=V::Obj){reason="document context changed";return false;}
    std::set<std::string> keys;for(const auto& field:beforeContext->obj)keys.insert(field.first);for(const auto& field:afterContext->obj)keys.insert(field.first);
    for(const auto& key:keys){
        const auto* before=beforeContext->find(key.c_str());const auto* after=afterContext->find(key.c_str());
        if(same(before,after))continue;
        // Only collections of named entries may grow; assembly nodes are keyed
        // by their ID because instance names need not be unique.
        const bool named=after&&after->t==V::Arr&&std::all_of(after->arr.begin(),after->arr.end(),[](const V& row){const auto* name=row.find("name");return name&&name->t==V::Str;});
        if(!named||!additionsOnly(before,*after,key=="assembly"?"id":"name","document "+key)){if(reason.empty())reason="document "+key+" changed";return false;}
    }
    return true;
}
