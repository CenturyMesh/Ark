static void spatialConstraintsDialog(){
    using V=arkjson::Value;const auto* selected=selectedPartFeature();if(!selected||selected->type!=FeatureType::Curve)throw std::runtime_error("Select an authored spatial curve first");const Feature source=*selected;const u64 revision=g_session.revision,epoch=g_documentEpoch;
    if(source.nativeSpec.find("op")&&source.nativeSpec.find("op")->asStr()!="curve")throw std::runtime_error("Edit the source of an equation/projected curve; point constraints apply to authored spatial segments");
    const auto* authored=source.nativeSpec.find("constraints");std::string initial=authored?authored->dump():"[{\"id\":\"origin\",\"type\":\"fixed\",\"point\":\"start\",\"value\":[0,0,0]}]";
    std::vector<EditorField> fields{{"3D constraints / JSON array",initial,true}};if(!editFields("3D sketch constraints","Handles: start, segment.1.end / through / pole.1. Types: fixed, coincident, distance, on_plane, on_axis, parallel, perpendicular, angle, equal_length, coplanar. Expressions remain driven; conflicts reject atomically.",fields))return;
    V constraints;if(!arkjson::parse(fields[0].value,constraints)||constraints.t!=V::Arr)throw std::runtime_error("Constraints must be a JSON array");V native=source.nativeSpec;
    if(constraints.arr.empty())native.obj.erase(std::remove_if(native.obj.begin(),native.obj.end(),[](const auto& entry){return entry.first=="constraints";}),native.obj.end());else{native.set("constraints",constraints);solveSpatialCurve(native,g_doc);}
    V command=V::makeObj();command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)source.id));command.set("native",native);applyUiCommandAtRevision(command,revision,epoch);
}
