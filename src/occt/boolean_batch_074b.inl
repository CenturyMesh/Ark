// Explicit all-operands Boolean mode. Every selected solid is retained in the
// construction; only the complete final result is subject to result_mode.
std::vector<TopoDS_Shape> checkedSolids074(const TopoDS_Shape& shape){
    if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())throw std::runtime_error("Batch Boolean input failed B-rep validation");
    auto solids=shapes(shape,TopAbs_SOLID);if(solids.empty()||solids.size()>64)throw std::runtime_error("Batch Boolean input needs 1..64 solids");
    TopTools_IndexedMapOfShape covered;
    for(const auto& solid:solids){if(!std::isfinite(properties(solid))||properties(solid)<=0)throw std::runtime_error("Batch Boolean input contains a non-positive solid");TopExp::MapShapes(solid,covered);}
    for(auto kind:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})for(const auto& item:shapes(shape,kind))if(!covered.Contains(item))throw std::runtime_error("Batch Boolean input contains loose geometry; nothing is discarded");
    return solids;
}
V bodyInventory074(V output,const TopoDS_Shape& shape){
    const auto solids=checkedSolids074(shape);const auto& identity=need(need(output,"topology"),"brep_sha256");V rows=V::makeArr();const auto faces=shapes(shape,TopAbs_FACE);
    for(size_t i=0;i<solids.size();++i){V row=V::makeObj(),box=V::makeArr(),indices=V::makeArr();row.set("index",V::makeNum(i));row.set("id",V::makeStr("@ark.solid."+identity.str+"."+std::to_string(i)));row.set("reference_scope",V::makeStr("current_brep_digest"));row.set("brep_sha256",identity);row.set("volume_mm3",V::makeNum(properties(solids[i])));for(double value:bounds(solids[i]))box.arr.push_back(V::makeNum(value));row.set("bounds_mm",std::move(box));for(size_t j=0;j<faces.size();++j)for(const auto& face:shapes(solids[i],TopAbs_FACE))if(face.IsSame(faces[j])){indices.arr.push_back(V::makeNum(j));break;}row.set("face_indices",std::move(indices));rows.arr.push_back(std::move(row));}
    V report=need(output,"report");report.set("bodies",rows);report.set("body_reference_scope",V::makeStr("current_brep_digest"));report.set("discarded_solids",V::makeNum(0));output.set("report",std::move(report));V topology=need(output,"topology");topology.set("solids",std::move(rows));output.set("topology",std::move(topology));return output;
}
V booleanBatch074(const V& sequence){
    if(sequence.t!=V::Obj||scalar(sequence,"schema")!=1)throw std::runtime_error("Unsupported all-input Boolean schema");
    const std::string op=word(sequence,"op"),mode=sequence.find("result_mode")?word(sequence,"result_mode"):"single";
    if(mode!="single"&&mode!="bodies")throw std::runtime_error("Boolean result_mode must be single or bodies");
    const auto& pieces=need(sequence,"pieces");if(pieces.t!=V::Arr||pieces.arr.size()<2||pieces.arr.size()>16)throw std::runtime_error("Batch Boolean needs 2..16 explicit inputs");
    std::vector<TopoDS_Shape> groups;std::vector<Ref> references;TopTools_ListOfShape arguments,tools,all;
    for(size_t i=0;i<pieces.arr.size();++i){const auto& piece=pieces.arr[i];auto shape=readBrep(word(piece,"brep_base64"));auto refs=inputRefs(piece,"brep_base64","topology",shape);shape=rigidlyPlaced(shape,piece,refs);const auto solids=checkedSolids074(shape);groups.push_back(shape);all.Append(shape);
        for(const auto& solid:solids){if(i==0)arguments.Append(solid);else tools.Append(solid);}
        const auto prefix=i?(piece.find("namespace")?word(piece,"namespace"):"input."+std::to_string(i))+".":std::string();for(auto& ref:refs){ref.name=prefix+ref.name;references.push_back(std::move(ref));}
    }
    TopoDS_Shape shape;std::vector<Ref> resultRefs;const bool parallel=nativeThreadBudget()>1;
    if(op=="common"){
        // COMMON's two-list API means A intersect UNION(tools). CellsBuilder
        // supplies the requested intersection of ALL explicitly selected inputs.
        BOPAlgo_CellsBuilder operation;operation.SetArguments(all);operation.SetNonDestructive(true);operation.SetRunParallel(parallel);operation.SetToFillHistory(true);operation.Perform();if(operation.HasErrors())throw std::runtime_error("All-input common partition failed");TopTools_ListOfShape avoid;operation.AddToResult(all,avoid,1,true);operation.MakeContainers();if(operation.HasErrors())throw std::runtime_error("All-input common construction failed");shape=operation.Shape();resultRefs=propagated(references,shape,operation);
    }else{
        if(op!="union"&&op!="cut")throw std::runtime_error("Batch Boolean operation must be union, cut or common");
        BRepAlgoAPI_BooleanOperation operation;operation.SetArguments(arguments);operation.SetTools(tools);operation.SetOperation(op=="union"?BOPAlgo_FUSE:BOPAlgo_CUT);operation.SetNonDestructive(true);operation.SetRunParallel(parallel);operation.Build();if(!operation.IsDone()||operation.HasErrors())throw std::runtime_error("All-input Boolean failed without tolerance fallback");shape=operation.Shape();resultRefs=propagated(references,shape,operation);
    }
    checkedSolids074(shape);V output=mode=="bodies"?finishBodies(shape,resultRefs):finish(shape,resultRefs);output=bodyInventory074(std::move(output),shape);V report=need(output,"report"),receipt=V::makeObj();receipt.set("mode",V::makeStr("all"));receipt.set("operation",V::makeStr(op));receipt.set("input_count",V::makeNum(pieces.arr.size()));receipt.set("result_mode",V::makeStr(mode));receipt.set("parallel_enabled",V::makeBool(parallel));receipt.set("native_threads",V::makeNum(nativeThreadBudget()));receipt.set("tolerance_changed",V::makeBool(false));receipt.set("history_propagated",V::makeBool(true));report.set("boolean",std::move(receipt));output.set("report",std::move(report));return output;
}
