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
    if(recipe.find("checks"))throw std::runtime_error("checks require a fresh explicit preview");
    const auto* commands=recipe.find("commands");if(!commands||commands->t!=V::Arr)throw std::runtime_error("missing commands");
    V authored;if(!arkjson::parse(documentToJson(document,false,false),authored))throw std::runtime_error("authoring unavailable");
    const V features=*authored.find("features");
    authored.obj.erase(std::remove_if(authored.obj.begin(),authored.obj.end(),[](const auto& field){return field.first=="features"||field.first=="next_feature_id"||field.first=="version";}),authored.obj.end());
    V bindings=V::makeArr();std::set<u64> reads,editedTargets;std::set<std::string> localNames;bool readsGeometricContext=false;
    auto resolve=[&](const V& selector)->u64{
        const Feature* selected=nullptr;
        if(selector.t==V::Num){if(!std::isfinite(selector.num)||selector.num<1||selector.num!=std::floor(selector.num)||selector.num>9007199254740990.)throw std::runtime_error("invalid numeric feature selector");selected=document.find((u64)selector.num);}
        else if(selector.t==V::Str){for(const auto& f:document.features)if(f.name==selector.str){if(selected)throw std::runtime_error("ambiguous feature name: "+selector.str);selected=&f;}}
        else throw std::runtime_error("unsupported feature selector");
        if(!selected)throw std::runtime_error("missing feature or provisional numeric ID; use retained existing IDs or earlier unique added names");
        V binding=V::makeObj();binding.set("selector",selector);binding.set("id",V::makeNum((double)selected->id));bindings.arr.push_back(std::move(binding));return selected->id;
    };
    auto geometricContext=[](const Feature& feature){return feature.type==FeatureType::Extrude||feature.type==FeatureType::Revolve||!feature.get("sketch").empty()||!feature.get("plane").empty()||feature.nativeSpec.dump().find("\"plane\"")!=std::string::npos;};
    std::function<void(u64)> include=[&](u64 id){if(!reads.insert(id).second)return;const auto* feature=document.find(id);if(!feature||!refreshFeatureSupported(*feature))throw std::runtime_error("feature dependency uses an unaudited or implicit read scope");readsGeometricContext|=geometricContext(*feature);for(auto input:feature->inputs)include(input);};
    auto input=[&](const V& selector){if(selector.t==V::Str&&localNames.count(selector.str))return;include(resolve(selector));};
    for(const auto& command:commands->arr){
        if(command.find("p_remove"))throw std::runtime_error("parameter removal requires a fresh explicit preview");
        const auto* operation=command.find("op");if(!operation||operation->t!=V::Str||(operation->str!="feature.add"&&operation->str!="feature.edit"))throw std::runtime_error("only feature.add and feature.edit recipes have refresh scopes");
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
    // Named sketches/datums have additional selectors (second_feature,
    // alternate_feature, projected supports, occurrence frames). Until their
    // full read graph is shared with the evaluator, retain the complete feature
    // context whenever the target OR an ancestor consumes named geometry.
    if(readsGeometricContext)for(const auto& feature:document.features)reads.insert(feature.id);
    V scope=V::makeObj(),selected=V::makeArr();
    for(const auto& row:features.arr){const auto* id=row.find("id");if(id&&reads.count((u64)id->num))selected.arr.push_back(row);}
    scope.set("context",authored);scope.set("features",selected);scope.set("bindings",bindings);
    V tessellation=V::makeArr();for(double x:{document.tessOpt.tol,document.tessOpt.maxEdge,document.tessOpt.minAngleDeg,(double)document.tessOpt.maxLevels})tessellation.arr.push_back(V::makeNum(x));scope.set("tessellation",tessellation);return scope;
}
