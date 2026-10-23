// B.03 / 059C. Exact derived producers; inputs stay immutable. No tessellation
// chooses a retained region or changes support geometry. Included after sew.
void sfKeys059(const V& request,std::initializer_list<const char*> keys) {
    if(request.t!=V::Obj)throw std::runtime_error("Surface request must be an object");
    std::set<std::string> allowed;for(auto k:keys)allowed.insert(k);
    for(const auto& item:request.obj)if(!allowed.count(item.first))throw std::runtime_error("Unknown surface field: "+item.first);
    tolerance(request);
}
TopoDS_Shape sfDecode059(const V& request,const char* key) {
    const auto& v=required(request,key);
    if(v.t!=V::Str||v.str.empty())throw std::runtime_error(std::string(key)+" requires exact B-rep");
    auto shape=readBrep(v.str);shapeReport(shape,false);return shape;
}
std::string sfWord059(const V& request,const char* key) {
    const auto& value=required(request,key);if(value.t!=V::Str||value.str.empty())throw std::runtime_error(std::string(key)+" requires text");return value.str;
}
std::string sfDigest059(const V& request,const char* key) {return sha256(base64Decode(required(request,key).str));}
bool sfOnPlane059(const TopoDS_Shape& shape,const gp_Trsf& toPlane) {
    const auto b=bounds(transformed(shape,toPlane));return std::abs(b[2])<=kTolerance&&std::abs(b[5])<=kTolerance;
}
void sfNativeCheck059(const TopoDS_Shape& shape,const char* label) {
    BRepAlgoAPI_Check check(shape,true,true);
    if(!check.IsValid())throw std::runtime_error(std::string(label)+" failed native small-edge/self-interference checks");
}
V mirrorSurface059(const V& request) {
    sfKeys059(request,{"op","input_brep","origin","normal","continuity","tolerance"});
    const auto source=sfDecode059(request,"input_brep");const auto faces=shapes(source,TopAbs_FACE);
    if(!shapes(source,TopAbs_SOLID).empty()||faces.empty()||faces.size()>128||
       (source.ShapeType()!=TopAbs_FACE&&source.ShapeType()!=TopAbs_SHELL))
        throw std::runtime_error("Surface mirror requires one face or connected shell of 1..128 faces");
    const auto requested=sfWord059(request,"continuity");
    if(requested!="G0"&&requested!="G1")throw std::runtime_error("Mirror continuity must be G0 or G1; the half surface is reflected, never reshaped");
    const auto originVector=vector031B(required(request,"origin"),"mirror origin"),normalVector=vector031B(required(request,"normal"),"mirror normal");
    if(normalVector.Magnitude()<1e-12)throw std::runtime_error("Mirror plane normal is zero");
    const gp_Pnt origin(originVector.XYZ());const gp_Dir normal(normalVector);
    gp_Trsf toPlane;toPlane.SetTransformation(gp_Ax3(origin,normal));
    const auto box=bounds(transformed(source,toPlane));
    const bool positive=box[2]>=-kTolerance&&box[5]>kTolerance,negative=box[5]<=kTolerance&&box[2]<-kTolerance;
    if(!positive&&!negative)throw std::runtime_error("Mirror source must occupy exactly one half-space and meet its plane; crossing/coplanar sources reject");
    NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> sourceOwners;
    TopExp::MapShapesAndAncestors(source,TopAbs_EDGE,TopAbs_FACE,sourceOwners);
    double originalSeamLength=0;unsigned originalSeams=0;
    for(int i=1;i<=sourceOwners.Extent();++i) {
        const auto edge=TopoDS::Edge(sourceOwners.FindKey(i));
        if(sourceOwners.FindFromIndex(i).Extent()==1&&!BRep_Tool::Degenerated(edge)&&sfOnPlane059(edge,toPlane)) {
            BRepAdaptor_Curve curve(edge);originalSeamLength+=GCPnts_AbscissaPoint::Length(curve,1e-8);++originalSeams;
        }
    }
    if(!originalSeams||originalSeams>64||!std::isfinite(originalSeamLength)||originalSeamLength<=kTolerance)
        throw std::runtime_error("Mirror needs 1..64 exact free boundary edges on its plane; point contact or a gap cannot join halves");
    sfNativeCheck059(source,"Mirror input");
    gp_Trsf reflection;reflection.SetMirror(gp_Ax2(origin,normal));
    const auto other=transformed(source,reflection);
    V sew=V::makeObj(),inputs=V::makeArr();inputs.arr={required(request,"input_brep"),V::makeStr(brepData(other))};
    sew.set("inputs",inputs);sew.set("solid",V::makeBool(false));sew.set("require_connected",V::makeBool(true));
    auto result=sewSurfaces(sew);const auto joined=readBrep(required(result,"brep_base64").str);
    if(shapes(joined,TopAbs_FACE).size()!=2*faces.size())throw std::runtime_error("Mirror sewing changed the authored face count");
    const double originalArea=properties(source,'a'),joinedArea=properties(joined,'a');
    if(std::abs(joinedArea-2*originalArea)>std::max(1e-6,originalArea*2e-8))throw std::runtime_error("Mirror area conservation failed");
    sfNativeCheck059(joined,"Mirrored surface");
    NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> owners;TopExp::MapShapesAndAncestors(joined,TopAbs_EDGE,TopAbs_FACE,owners);
    auto report=required(result,"report");const auto& continuity=required(report,"shared_edge_continuity");
    V seams=V::makeArr();double maxGap=0,maxAngle=0,seamLength=0;
    for(const auto& row:continuity.arr) {
        const int index=(int)required(row,"edge_index").num+1;
        if(index<1||index>owners.Extent())throw std::runtime_error("Mirror continuity topology receipt is inconsistent");
        const auto edge=TopoDS::Edge(owners.FindKey(index));if(!sfOnPlane059(edge,toPlane))continue;
        if(required(row,"status").str!="measured"||required(row,"samples_checked").num!=17)
            throw std::runtime_error("Mirror seam continuity is unresolved; no G0/G1 claim or tolerance relaxation is permitted");
        const double gap=required(row,"observed_max_gap_mm").num,angle=required(row,"observed_max_tangent_plane_angle_deg").num;
        if(gap>kTolerance)throw std::runtime_error("Mirror seam exceeds the fixed 1e-6 mm G0 bound");
        if(requested=="G1"&&angle>.001)throw std::runtime_error("Mirror half is not tangent at its seam (G1 bound 0.001 degrees); edit the source controls explicitly");
        BRepAdaptor_Curve curve(edge);seamLength+=GCPnts_AbscissaPoint::Length(curve,1e-8);
        maxGap=std::max(maxGap,gap);maxAngle=std::max(maxAngle,angle);seams.arr.push_back(row);
    }
    if(seams.arr.empty()||std::abs(seamLength-originalSeamLength)>std::max(1e-6,originalSeamLength*1e-8))
        throw std::runtime_error("Mirror did not join every authored symmetry boundary");
    report.set("operation",V::makeStr("surface_mirror"));report.set("source_brep_sha256",V::makeStr(sfDigest059(request,"input_brep")));
    report.set("symmetry_origin_mm",required(request,"origin"));report.set("symmetry_normal",vec(normal.XYZ()));
    report.set("source_half",V::makeStr(positive?"positive":"negative"));report.set("continuity_requested",V::makeStr(requested));
    report.set("symmetry_seams",seams);report.set("symmetry_seam_length_mm",V::makeNum(seamLength));
    report.set("observed_max_seam_gap_mm",V::makeNum(maxGap));report.set("observed_max_seam_tangent_angle_deg",V::makeNum(maxAngle));
    report.set("g1_angular_bound_deg",V::makeNum(.001));report.set("area_conservation_checked",V::makeBool(true));
    report.set("source_geometry_modified",V::makeBool(false));report.set("native_self_interference_check",V::makeBool(true));
    report.set("global_g1_verified",V::makeBool(false));report.set("global_g2_verified",V::makeBool(false));
    report.set("continuity_scope",V::makeStr("17 interior native pcurve checkpoints per joined symmetry edge; no endpoint/extremum/global G1 or G2 proof; no automatic smoothing"));
    result.set("report",report);return result;
}

V trimSurfaceTool059(const V& request) {
    sfKeys059(request,{"op","input_brep","tool_brep","tool_kind","keep_point","tolerance"});
    const auto source=singleSurface031B(required(request,"input_brep"),"Trim input");
    const auto tool=sfDecode059(request,"tool_brep");const auto kind=sfWord059(request,"tool_kind");
    if(kind=="surface") {
        if(!shapes(tool,TopAbs_SOLID).empty()||shapes(tool,TopAbs_FACE).size()!=1||tool.ShapeType()!=TopAbs_FACE)
            throw std::runtime_error("Surface cutter requires one exact bounded face; extend its support explicitly when needed");
    } else if(kind=="solid") {
        if(tool.ShapeType()!=TopAbs_SOLID)throw std::runtime_error("Solid cutter requires exactly one positive-volume solid");requireOnePositiveSolid(tool);
    } else if(kind=="curve") {
        if(tool.ShapeType()!=TopAbs_WIRE||shapes(tool,TopAbs_WIRE).size()!=1||shapes(tool,TopAbs_EDGE).empty()||shapes(tool,TopAbs_EDGE).size()>128)
            throw std::runtime_error("Curve cutter requires one exact connected wire with 1..128 edges");
    } else throw std::runtime_error("tool_kind must be surface, solid or curve");
    sfNativeCheck059(source,"Trim source");sfNativeCheck059(tool,"Trim cutter");
    const auto keep=vector031B(required(request,"keep_point"),"keep point");const gp_Pnt at(keep.XYZ());
    NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(source);tools.Append(tool);
    BRepAlgoAPI_Splitter split;split.SetArguments(arguments);split.SetTools(tools);split.SetNonDestructive(true);split.Build();
    if(!split.IsDone()||split.HasErrors())throw std::runtime_error("Exact surface/tool split failed");
    const auto pieces=shapes(split.Shape(),TopAbs_FACE);
    if(pieces.size()<2||pieces.size()>64)throw std::runtime_error("Cutter must divide the source into 2..64 exact regions; project a curve onto its support or extend the cutter explicitly");
    TopoDS_Shape selected;unsigned matches=0,selectedIndex=0;double totalArea=0;V regions=V::makeArr();
    for(size_t i=0;i<pieces.size();++i) {
        const auto& piece=pieces[i];shapeReport(piece,false);double area=properties(piece,'a');
        if(!std::isfinite(area)||area<=1e-8)throw std::runtime_error("Surface trim produced an empty/degenerate region");totalArea+=area;
        bool retained=false;BRepClass_FaceClassifier classify(TopoDS::Face(piece),at,kTolerance);
        if(classify.State()==TopAbs_IN) {
            BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(at).Vertex(),piece);distance.Perform();
            if(distance.IsDone()&&distance.Value()<=kTolerance){selected=piece;++matches;selectedIndex=(unsigned)i;retained=true;}
        }
        V row=V::makeObj();row.set("region",V::makeNum(double(i)));row.set("area_mm2",V::makeNum(area));row.set("retained",V::makeBool(retained));regions.arr.push_back(row);
    }
    if(matches!=1)throw std::runtime_error("keep_point must lie strictly inside exactly one retained region and on the original surface, away from cutter boundaries");
    const double originalArea=properties(source,'a'),retainedArea=properties(selected,'a');
    if(std::abs(totalArea-originalArea)>std::max(1e-6,originalArea*1e-8)||originalArea-retainedArea<=1e-8)
        throw std::runtime_error("Surface trim failed complete area conservation; no region was discarded implicitly");
    sfNativeCheck059(selected,"Trim result");
    auto result=surfaceEditResult(selected,"surface_trim_tool"),report=required(result,"report");
    report.set("tool_kind",V::makeStr(kind));report.set("source_brep_sha256",V::makeStr(sfDigest059(request,"input_brep")));report.set("tool_brep_sha256",V::makeStr(sfDigest059(request,"tool_brep")));
    report.set("keep_point_mm",required(request,"keep_point"));report.set("retained_region",V::makeNum(selectedIndex));report.set("regions",regions);
    report.set("original_area_mm2",V::makeNum(originalArea));report.set("retained_area_mm2",V::makeNum(retainedArea));report.set("removed_area_mm2",V::makeNum(originalArea-retainedArea));
    report.set("split_regions",V::makeNum(double(pieces.size())));report.set("area_conservation_checked",V::makeBool(true));
    report.set("selection_authority",V::makeStr("native face classification and exact point-to-face distance; explicit keep point"));
    report.set("native_self_interference_check",V::makeBool(true));report.set("tool_projected_or_extended",V::makeBool(false));result.set("report",report);return result;
}

TopoDS_Edge sfSingleCurve059(const V& request,const char* key) {
    const auto wire=sfDecode059(request,key);const auto edges=shapes(wire,TopAbs_EDGE);
    if(wire.ShapeType()!=TopAbs_WIRE||edges.size()!=1||BRep_Tool::IsClosed(wire))
        throw std::runtime_error(std::string(key)+" requires one exact open single-edge wire; explicit composite section mapping is not supported");
    sfNativeCheck059(wire,"Ruled source curve");const auto edge=TopoDS::Edge(edges.front());BRepAdaptor_Curve curve(edge);
    if(!std::isfinite(curve.FirstParameter())||!std::isfinite(curve.LastParameter())||GCPnts_AbscissaPoint::Length(curve,1e-8)<=kTolerance)
        throw std::runtime_error("Ruled source requires a finite nondegenerate 3D curve");
    return edge;
}
V ruledSurface059(const V& request) {
    sfKeys059(request,{"op","first_brep","second_brep","reverse_second","tolerance"});
    const auto first=sfSingleCurve059(request,"first_brep");auto second=sfSingleCurve059(request,"second_brep");
    const auto& reverse=required(request,"reverse_second");if(reverse.t!=V::Bool)throw std::runtime_error("reverse_second must explicitly select the ruled correspondence");
    if(reverse.b)second.Reverse();
    BRepExtrema_DistShapeShape separation(first,second);separation.Perform();
    if(!separation.IsDone()||separation.Value()<=kTolerance)throw std::runtime_error("Ruled source curves touch/intersect; a nonzero span is required");
    const auto face=BRepFill::Face(first,second);shapeReport(face,false);sfNativeCheck059(face,"Ruled surface");
    if(properties(face,'a')<=1e-8)throw std::runtime_error("Ruled surface has no positive area");
    double error=0;unsigned samples=0;
    for(const auto& edge:{first,second}) {
        BRepAdaptor_Curve curve(edge);
        for(int i=0;i<=32;++i) {
            const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/32.);
            BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(),face);distance.Perform();
            if(!distance.IsDone())throw std::runtime_error("Ruled source preservation check failed");error=std::max(error,distance.Value());++samples;
        }
    }
    if(error>kTolerance)throw std::runtime_error("Ruled surface does not preserve its exact source curves within 1e-6 mm");
    auto result=surfaceEditResult(face,"surface_ruled"),report=required(result,"report");
    report.set("first_brep_sha256",V::makeStr(sfDigest059(request,"first_brep")));report.set("second_brep_sha256",V::makeStr(sfDigest059(request,"second_brep")));
    report.set("reverse_second",reverse);report.set("source_preservation_samples",V::makeNum(samples));report.set("observed_max_source_error_mm",V::makeNum(error));
    report.set("minimum_source_separation_mm",V::makeNum(separation.Value()));report.set("construction",V::makeStr("OCCT BRepFill ruled interpolation between explicitly oriented native 3D edges"));
    report.set("native_self_interference_check",V::makeBool(true));report.set("tangent_support_constraints",V::makeBool(false));
    report.set("scope",V::makeStr("One open edge per input; ruled cross-span, source curves preserved at 66 native checkpoints; no G1/G2 support claim"));
    result.set("report",report);return result;
}
