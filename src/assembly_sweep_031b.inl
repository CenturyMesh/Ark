// Included inside namespace ark by modeling_031b.cpp. Occurrence-aware cable
// bindings and sampled mechanism inspection share existing producer contracts.
namespace {
int copy031(const V& object,const char* field="copy"){
    const auto* value=object.find(field);if(!value)return 0;
    if(value->t!=V::Num||!std::isfinite(value->num)||value->num<0||value->num>31||value->num!=std::floor(value->num))throw std::runtime_error("031B occurrence copy must be integer 0..31");return (int)value->num;
}
u64 stableBindingNode031(const Document& document,const V& item){
    const auto& node=required031(item,"node");if(node.t!=V::Num)throw std::runtime_error("Persistent cable occurrence bindings require a stable numeric node ID; names are only accepted by read-only sweep selections");return node031(document,node);
}
void finitePose031(const Mat4& pose){for(double value:pose.m)if(!std::isfinite(value)||std::abs(value)>1e8)throw std::runtime_error("031B occurrence pose must be finite and bounded");}
const AssemblyPlacement& placement031(const std::vector<AssemblyPlacement>& pose,u64 node,int copy){
    const AssemblyPlacement* match=nullptr;for(const auto& occurrence:pose)if(occurrence.node==node&&occurrence.copy==copy){if(match)throw std::runtime_error("Ambiguous 031B occurrence/copy");match=&occurrence;}
    if(!match)throw std::runtime_error("031B occurrence/copy is not placed");finitePose031(match->world);return *match;
}
const Body& body031(const Document& document,u64 feature){const auto& bodies=document.evaluatingBodies?*document.evaluatingBodies:document.evalBodies;for(const auto& body:bodies)if(body.id==feature)return body;throw std::runtime_error("031B occurrence body is not evaluated");}
bool dependsOn031(const Document& document,u64 feature,u64 target,std::set<u64>& seen){
    if(feature==target)return true;if(!seen.insert(feature).second)return false;
    const auto* producer=document.find(feature);if(!producer)throw std::runtime_error("031B occurrence producer is missing");
    for(auto source:producer->inputs)if(dependsOn031(document,source,target,seen))return true;return false;
}
void cableDependency031(const Document& document,const Feature& cable,u64 producer){
    std::set<u64> seen;if(dependsOn031(document,producer,cable.id,seen))throw std::runtime_error("Cable occurrence binding has a self-dependency");
    if(std::find(cable.inputs.begin(),cable.inputs.end(),producer)==cable.inputs.end())throw std::runtime_error("Cable occurrence producer must be retained explicitly in inputs");
}
AABB obstacleBox031(const Body& body,const Mat4& world,std::string& authority){
    AABB bounds;
    if(const auto* report=body.nativeData.find("report"))if(const auto* values=report->find("bounds_mm")){
        if(values->t!=V::Arr||values->arr.size()!=6)throw std::runtime_error("031B native obstacle bounds are invalid");
        for(const auto& value:values->arr)if(value.t!=V::Num||!std::isfinite(value.num))throw std::runtime_error("031B native obstacle bounds are nonfinite");
        bounds.mn={values->arr[0].num,values->arr[1].num,values->arr[2].num};bounds.mx={values->arr[3].num,values->arr[4].num,values->arr[5].num};authority="native_brep_report_local_bounds_transformed_to_world_AABB";
    }
    if(!bounds.valid()){bounds=body.bbox();authority="evaluated_geometry_local_bounds_transformed_to_world_AABB";}
    if(!bounds.valid())throw std::runtime_error("031B obstacle body has no measurable bounds");
    bounds=bounds.transformed(world);if(!bounds.valid()||bounds.mx.x<=bounds.mn.x||bounds.mx.y<=bounds.mn.y||bounds.mx.z<=bounds.mn.z)throw std::runtime_error("031B obstacle requires positive three-dimensional AABB; no thickness is inferred");return bounds;
}
}

V resolveCableBindings031B(const Feature& cable,const ExprEnv& env,const Document& document,const std::vector<AssemblyPlacement>* suppliedPose){
    if(cable.type!=FeatureType::CableRoute||cable.nativeSpec.t!=V::Obj)throw std::runtime_error("031B cable bindings require an authored cable_route");
    V resolved=cable.nativeSpec;bool hasBindings=false;
    auto bound=[](const V& item){return item.t==V::Obj&&item.find("node");};
    for(const char* key:{"start","end"})if(const auto* point=resolved.find(key))hasBindings|=bound(*point);
    for(const char* key:{"waypoints","obstacles"})if(const auto* list=resolved.find(key)){if(list->t!=V::Arr)throw std::runtime_error(std::string("Cable ")+key+" must be an array");for(const auto& item:list->arr)hasBindings|=bound(item);}
    if(!hasBindings)return resolved;
    std::vector<AssemblyPlacement> baseline;
    if(!suppliedPose){std::string error;if(!resolveAssembly(document,baseline,error))throw std::runtime_error("Cable occurrence solve failed: "+error);suppliedPose=&baseline;}
    V bindingReport=V::makeObj(),endpoints=V::makeArr(),obstacles=V::makeArr();
    auto endpoint=[&](const V& item,const std::string& role){
        if(!bound(item))return item;
        for(const auto& field:item.obj)if(field.first!="node"&&field.first!="copy"&&field.first!="reference"&&field.first!="offset"&&field.first!="name")throw std::runtime_error("Unknown cable occurrence endpoint field: "+field.first);
        const auto node=stableBindingNode031(document,item);const int copy=copy031(item);const auto& occurrence=placement031(*suppliedPose,node,copy);cableDependency031(document,cable,occurrence.feature);
        std::string reference="origin";if(const auto* value=item.find("reference")){if(value->t!=V::Str||value->str.empty()||value->str.size()>128)throw std::runtime_error("Cable binding reference must be a semantic name");reference=value->str;}
        Mat4 local=Mat4::identity();if(reference!="origin")local=resolveGeometryReference(document,occurrence.feature,reference).frame;finitePose031(local);
        const Vec3 offset=item.find("offset")?point031(*item.find("offset"),env):Vec3{};
        const Vec3 point=occurrence.world.transformPoint(local.transformPoint(offset));
        if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||std::max({std::abs(point.x),std::abs(point.y),std::abs(point.z)})>10000)throw std::runtime_error("Cable bound point exceeds finite 10 m domain");
        std::string name=role;if(const auto* value=item.find("name")){if(value->t!=V::Str||value->str.empty()||value->str.size()>128)throw std::runtime_error("Cable bound endpoint name must be nonempty");name=value->str;}
        V result=V::makeObj();result.set("name",V::makeStr(name));result.set("point",xyz031(point));
        V row=V::makeObj();row.set("role",V::makeStr(role));row.set("node",V::makeNum((double)node));row.set("copy",V::makeNum(copy));row.set("feature",V::makeNum((double)occurrence.feature));row.set("reference",V::makeStr(reference));row.set("world_point_mm",xyz031(point));endpoints.arr.push_back(row);return result;
    };
    for(const char* key:{"start","end"})if(const auto* item=resolved.find(key)){const V result=endpoint(*item,key);resolved.set(key,result);}
    if(const auto* source=resolved.find("waypoints")){if(source->arr.size()>32)throw std::runtime_error("Cable waypoint limit is 32");V points=V::makeArr();for(size_t i=0;i<source->arr.size();++i)points.arr.push_back(endpoint(source->arr[i],"waypoint."+std::to_string(i)));resolved.set("waypoints",points);}
    if(const auto* source=resolved.find("obstacles")){
        if(source->arr.size()>64)throw std::runtime_error("Cable obstacle limit is 64");V boxes=V::makeArr();std::set<std::pair<u64,int>> seen;
        for(const auto& item:source->arr){if(!bound(item)){boxes.arr.push_back(item);continue;}
            for(const auto& field:item.obj)if(field.first!="node"&&field.first!="copy"&&field.first!="name")throw std::runtime_error("Unknown cable occurrence obstacle field: "+field.first);
            const auto node=stableBindingNode031(document,item);const int copy=copy031(item);if(!seen.insert({node,copy}).second)throw std::runtime_error("Duplicate cable obstacle occurrence/copy");
            const auto& occurrence=placement031(*suppliedPose,node,copy);cableDependency031(document,cable,occurrence.feature);std::string authority;const auto box=obstacleBox031(body031(document,occurrence.feature),occurrence.world,authority);
            std::string name="occurrence."+std::to_string(node)+"."+std::to_string(copy);if(const auto* value=item.find("name")){if(value->t!=V::Str||value->str.empty()||value->str.size()>128)throw std::runtime_error("Cable obstacle name must be nonempty");name=value->str;}
            V result=V::makeObj();result.set("name",V::makeStr(name));result.set("min",xyz031(box.mn));result.set("max",xyz031(box.mx));boxes.arr.push_back(result);
            V row=V::makeObj();row.set("node",V::makeNum((double)node));row.set("copy",V::makeNum(copy));row.set("feature",V::makeNum((double)occurrence.feature));row.set("bounds_authority",V::makeStr(authority));row.set("min_mm",xyz031(box.mn));row.set("max_mm",xyz031(box.mx));row.set("conservative_AABB",V::makeBool(true));obstacles.arr.push_back(row);
        }resolved.set("obstacles",boxes);
    }
    bindingReport.set("endpoints",endpoints);bindingReport.set("obstacles",obstacles);bindingReport.set("coordinate_frame",V::makeStr("assembly_world_mm"));bindingReport.set("obstacle_model",V::makeStr("conservative_world_AABB_not_exact_collision"));bindingReport.set("dynamic_physics",V::makeBool(false));resolved.set("occurrence_binding_report",bindingReport);return resolved;
}

V inspectAssemblySweep031B(const Document& document,const V& args){
    if(args.t!=V::Obj)throw std::runtime_error("Assembly sweep requires an object");
    for(const auto& field:args.obj)if(field.first!="driver"&&field.first!="mate"&&field.first!="mode"&&field.first!="from"&&field.first!="to"&&field.first!="samples"&&field.first!="pairs"&&field.first!="cables"&&field.first!="collision_mode")throw std::runtime_error("Unknown assembly sweep field: "+field.first);
    const auto driver=node031(document,required031(args,"driver"));const auto env=document.buildEnv();
    const auto& count=required031(args,"samples");if(count.t!=V::Num||!std::isfinite(count.num)||count.num<2||count.num>65||count.num!=std::floor(count.num))throw std::runtime_error("Assembly sweep samples must be integer 2..65");
    const int samples=(int)count.num;const double from=scalar031(required031(args,"from"),env),to=scalar031(required031(args,"to"),env);
    u64 mate=0;if(const auto* value=args.find("mate")){if(value->t!=V::Num||!std::isfinite(value->num)||value->num<1||value->num>9007199254740990.||value->num!=std::floor(value->num))throw std::runtime_error("Assembly sweep mate must be a positive integer");mate=(u64)value->num;}
    std::string mode;if(const auto* value=args.find("mode")){if(value->t!=V::Str||(value->str!="rotate"&&value->str!="translate"))throw std::runtime_error("Assembly sweep mode must be rotate or translate");mode=value->str;}
    std::string collisionMode="exact";if(const auto* value=args.find("collision_mode")){if(value->t!=V::Str||(value->str!="exact"&&value->str!="mesh"))throw std::runtime_error("Assembly sweep collision_mode must be exact or mesh");collisionMode=value->str;}
    AssemblyManipulationProgram program;std::string error;if(!buildAssemblyManipulation(document,program,error))throw std::runtime_error("Assembly sweep baseline failed: "+error);
    struct Pair{u64 a,b;int ca,cb;};std::vector<Pair> pairs;std::vector<const Feature*> cables;
    if(const auto* list=args.find("pairs")){
        if(list->t!=V::Arr||list->arr.size()>64)throw std::runtime_error("Assembly sweep pair limit is 64");std::set<std::pair<std::pair<u64,int>,std::pair<u64,int>>> seen;
        for(const auto& item:list->arr){if(item.t!=V::Obj)throw std::runtime_error("Assembly sweep pair must be object");for(const auto& field:item.obj)if(field.first!="a"&&field.first!="b"&&field.first!="copy_a"&&field.first!="copy_b")throw std::runtime_error("Unknown assembly sweep pair field: "+field.first);
            Pair pair{node031(document,required031(item,"a")),node031(document,required031(item,"b")),copy031(item,"copy_a"),copy031(item,"copy_b")};auto first=std::pair<u64,int>{pair.a,pair.ca},second=std::pair<u64,int>{pair.b,pair.cb};if(first==second)throw std::runtime_error("Assembly sweep requires distinct collision occurrences");if(first>second)std::swap(first,second);if(!seen.insert({first,second}).second)throw std::runtime_error("Duplicate assembly sweep collision pair");
            const auto& a=placement031(program.baseline,pair.a,pair.ca);const auto& b=placement031(program.baseline,pair.b,pair.cb);
            if(collisionMode=="exact"){auto& mutableDocument=const_cast<Document&>(document);auto& bodies=document.evaluatingBodies?*document.evaluatingBodies:mutableDocument.evalBodies;ensureNativeBody(mutableDocument,a.feature,bodies);ensureNativeBody(mutableDocument,b.feature,bodies);if(!body031(document,a.feature).nativeData.find("brep_base64")||!body031(document,b.feature).nativeData.find("brep_base64"))throw std::runtime_error("Exact assembly sweep requires native solid bodies; select mesh mode explicitly for triangle inputs");}
            pairs.push_back(pair);
        }
    }
    if(const auto* list=args.find("cables")){
        if(list->t!=V::Arr||list->arr.size()>32)throw std::runtime_error("Assembly sweep cable limit is 32");std::set<u64> seen;
        for(const auto& value:list->arr){if(value.t!=V::Num||!std::isfinite(value.num)||value.num<1||value.num>9007199254740990.||value.num!=std::floor(value.num)||!seen.insert((u64)value.num).second)throw std::runtime_error("Assembly sweep cables require unique positive feature IDs");const auto* feature=document.find((u64)value.num);if(!feature||feature->type!=FeatureType::CableRoute||feature->suppressed)throw std::runtime_error("Assembly sweep cable feature is missing, suppressed or has wrong type");cables.push_back(feature);}
    }
    if((pairs.size()+cables.size())*(size_t)samples>4096)throw std::runtime_error("Assembly sweep exceeds 4096 combined pair/cable sample checks; no partial result published");
    V result=V::makeObj(),poses=V::makeArr();size_t interfering=0,unknown=0;
    for(int i=0;i<samples;++i){
        const double delta=from+(to-from)*((double)i/(samples-1));AssemblyManipulationPreview preview;
        if(!previewAssemblyManipulation(program,driver,mate,mode,delta,preview,error))throw std::runtime_error("Assembly sweep sample "+std::to_string(i)+" failed: "+error);
        for(const auto& placement:preview.placements)finitePose031(placement.world);
        V row=V::makeObj(),pairReports=V::makeArr(),cableReports=V::makeArr();row.set("index",V::makeNum(i));row.set("delta",V::makeNum(delta));row.set("joint_coordinate",V::makeNum(preview.joint.position+delta*preview.joint.coordinateSign));row.set("maximum_mate_position_residual_mm",V::makeNum(preview.diagnostics.maxPositionMm));row.set("maximum_mate_angle_residual_deg",V::makeNum(preview.diagnostics.maxAngleDeg));
        for(const auto& pair:pairs){const auto& a=placement031(preview.placements,pair.a,pair.ca);const auto& b=placement031(preview.placements,pair.b,pair.cb);const auto& ba=body031(document,a.feature);const auto& bb=body031(document,b.feature);V measured;
            if(collisionMode=="exact")measured=inspectNativeCollision(ba,a.world,bb,b.world);
            else{const Mesh ma=ba.baked&&ba.baked->triCount()?*ba.baked:ba.tessellate(document.tessOpt),mb=bb.baked&&bb.baked->triCount()?*bb.baked:bb.tessellate(document.tessOpt);measured=inspectMeshCollision(ma,a.world,mb,b.world);measured.set("sampled_from_brep_a",V::makeBool(ba.nativeData.find("brep_base64")!=nullptr));measured.set("sampled_from_brep_b",V::makeBool(bb.nativeData.find("brep_base64")!=nullptr));}
            if(const auto* contact=measured.find("interference")){if(contact->t==V::Bool){if(contact->b)++interfering;}else ++unknown;}else ++unknown;
            V entry=V::makeObj();entry.set("a",V::makeNum((double)pair.a));entry.set("b",V::makeNum((double)pair.b));entry.set("copy_a",V::makeNum(pair.ca));entry.set("copy_b",V::makeNum(pair.cb));entry.set("measurement",measured);pairReports.arr.push_back(entry);
        }
        for(const auto* cable:cables){
            Feature bound=*cable;bound.nativeSpec=resolveCableBindings031B(*cable,env,document,&preview.placements);const V normalized=electronicsRequest(bound,env);
            V request=V::makeObj();request.set("operation",V::makeStr("model_electronics"));request.set("request",normalized);
            // Rounded bends can enter a box even when both incident straight
            // centerline segments are clear. Build and check the actual swept
            // native cable at every pose, retaining the box's conservative scope.
            const V native=nativeWorkerRequest(request);V entry=V::makeObj();entry.set("feature",V::makeNum((double)cable->id));entry.set("report",required031(native,"electronics"));entry.set("geometry",required031(native,"report"));if(const auto* bindings=bound.nativeSpec.find("occurrence_binding_report"))entry.set("occurrence_bindings",*bindings);cableReports.arr.push_back(entry);
        }
        row.set("pairs",pairReports);row.set("cables",cableReports);poses.arr.push_back(row);
    }
    result.set("samples",poses);result.set("sample_count",V::makeNum(samples));result.set("driver",V::makeNum((double)driver));result.set("from_delta",V::makeNum(from));result.set("to_delta",V::makeNum(to));result.set("collision_mode",V::makeStr(collisionMode));result.set("interfering_pair_samples",V::makeNum((double)interfering));result.set("unknown_pair_samples",V::makeNum((double)unknown));result.set("all_sampled_pairs_separated_or_touching",V::makeBool(!interfering&&!unknown));result.set("complete_requested_samples",V::makeBool(true));result.set("sampled_poses_only",V::makeBool(true));result.set("between_sample_clearance_checked",V::makeBool(false));result.set("dynamic_physics",V::makeBool(false));result.set("cable_elasticity_checked",V::makeBool(false));result.set("manufacturing_qualified",V::makeBool(false));result.set("document_unchanged",V::makeBool(true));return result;
}
