// A split line changes the face partition of one solid, without removing or
// adding material. The sketch's exact wire and the retained source B-rep are
// the authority; no tessellation participates in projection or face splitting.
namespace {
constexpr double splitLineTolerance=1e-6;
struct SplitProjection {TopoDS_Wire wire;double first=0,last=0;};
void splitTolerance(const TopoDS_Shape& shape,const char* label){
    for(const auto& face:shapes(shape,TopAbs_FACE))if(BRep_Tool::Tolerance(TopoDS::Face(face))>splitLineTolerance)throw std::runtime_error(std::string(label)+" face tolerance exceeds fixed 1e-6 mm split-line tolerance");
    for(const auto& edge:shapes(shape,TopAbs_EDGE))if(BRep_Tool::Tolerance(TopoDS::Edge(edge))>splitLineTolerance)throw std::runtime_error(std::string(label)+" edge tolerance exceeds fixed 1e-6 mm split-line tolerance");
    for(const auto& vertex:shapes(shape,TopAbs_VERTEX))if(BRep_Tool::Tolerance(TopoDS::Vertex(vertex))>splitLineTolerance)throw std::runtime_error(std::string(label)+" vertex tolerance exceeds fixed 1e-6 mm split-line tolerance");
}
GProp_GProps splitAreaProperties(const TopoDS_Shape& shape){GProp_GProps properties;const auto error=BRepGProp::SurfaceProperties(shape,properties,1e-12,false);if(!std::isfinite(error)||error<0||error>1e-10)throw std::runtime_error("Split-line adaptive exact surface integration failed its fixed precision");return properties;}
double splitArea(const TopoDS_Shape& shape){return splitAreaProperties(shape).Mass();}
std::array<double,4> splitRegionOrder(const TopoDS_Shape& face){const auto p=splitAreaProperties(face);const auto c=p.CentreOfMass();return {c.X(),c.Y(),c.Z(),p.Mass()};}
TopoDS_Shape splitNativeFace(const TopoDS_Shape& input,const V& request,std::vector<Ref>& refs,V& receipt){
    requireOneSolid(input,"Split line");splitTolerance(input,"Split-line source");
    const auto sourceHash=digest(word(request,"input_brep"));
    if(word(request,"source_topology")!=sourceHash)throw std::runtime_error("Split-line source topology changed; inspect and select the current exact face");
    const auto faceName=word(request,"source_face");const Ref* selected=nullptr;
    for(const auto& ref:refs)if(ref.name==faceName){if(selected)throw std::runtime_error("Ambiguous split-line source face");selected=&ref;}
    if(!selected||selected->kind!="face"||!selectable(selected->status)||selected->shape.IsNull())throw std::runtime_error("Split-line face is missing, obsolete, split or deleted: "+faceName);
    const auto target=TopoDS::Face(selected->shape);const BRepAdaptor_Surface surface(target);
    if(surface.GetType()!=GeomAbs_Plane&&surface.GetType()!=GeomAbs_Cylinder)throw std::runtime_error("Split line currently supports exact planar and cylindrical target faces only");
    const auto& profile=need(request,"profile");const auto sketchFrame=frame(profile);const auto source=profileCurveNative(profile);const auto sourceShape=readBrep(word(source,"brep_base64"));const auto sourceWires=shapes(sourceShape,TopAbs_WIRE);
    if(sourceWires.size()!=1)throw std::runtime_error("Split line requires one connected exact sketch wire");const auto sourceWire=TopoDS::Wire(sourceWires.front());
    if(shapes(sourceWire,TopAbs_EDGE).size()>128)throw std::runtime_error("Split line supports at most 128 exact sketch edges");splitTolerance(sourceWire,"Split-line sketch");
    const auto inverse=transform(sketchFrame).Inverted();const auto sourceBox=bounds(transformed(sourceWire,inverse));
    if(std::abs(sourceBox[2])>splitLineTolerance||std::abs(sourceBox[5])>splitLineTolerance)throw std::runtime_error("Split-line source wire must lie on its declared sketch plane");
    const auto vector=point(need(request,"direction"),"Split-line direction");const gp_Vec projectionVector(vector.X(),vector.Y(),vector.Z());
    if(projectionVector.Magnitude()<1e-12)throw std::runtime_error("Split-line projection direction must be nonzero");const gp_Dir projectionDirection(projectionVector);
    const gp_Vec planeNormal(sketchFrame[8],sketchFrame[9],sketchFrame[10]);const double slope=gp_Vec(projectionDirection).Dot(planeNormal);
    if(std::abs(slope)<1e-10)throw std::runtime_error("Split-line projection direction is parallel to the sketch plane");
    std::vector<SplitProjection> candidates;std::string representation="OCCT_directional_surface_projection";
    if(surface.GetType()==GeomAbs_Plane&&planeNormal.Crossed(gp_Vec(surface.Plane().Axis().Direction())).Magnitude()<1e-10){
        // Parallel planes have one exact constant translation, including a
        // sketch already on the target. Preserve its analytic/rational curves
        // rather than building an approximate intersection curve for this case.
        const auto targetPlane=surface.Plane();const gp_Vec normal(targetPlane.Axis().Direction());const gp_Pnt origin(sketchFrame[12],sketchFrame[13],sketchFrame[14]);const double travel=-gp_Vec(targetPlane.Location(),origin).Dot(normal)/gp_Vec(projectionDirection).Dot(normal);
        if(travel < -splitLineTolerance)throw std::runtime_error("Split-line sketch has no forward intersection with the selected face");gp_Trsf translation;translation.SetTranslation(gp_Vec(projectionDirection)*travel);const auto projectedSource=transformed(sourceWire,translation);
        BRepAlgoAPI_Common clipping(projectedSource,target);clipping.Build();if(!clipping.IsDone()||clipping.HasErrors())throw std::runtime_error("Split-line exact planar face clipping failed");
        auto edges=new TopTools_HSequenceOfShape;for(const auto& edge:shapes(clipping.Shape(),TopAbs_EDGE))edges->Append(edge);occ::handle<TopTools_HSequenceOfShape> wires=new TopTools_HSequenceOfShape;ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges,1e-7,false,wires);
        for(int i=1;i<=wires->Length();++i)candidates.push_back({TopoDS::Wire(wires->Value(i)),travel,travel});representation="exact_parallel_plane_translation_and_bounded_face_intersection";
    }else{
        BRepProj_Projection projection(sourceWire,target,projectionDirection);
        if(!projection.IsDone())throw std::runtime_error("Split-line directional projection failed");
        for(projection.Init();projection.More();projection.Next()){
            const auto wire=projection.Current();if(candidates.size()>=16)throw std::runtime_error("Split-line projection exceeds 16 branches");
            const auto box=bounds(transformed(wire,inverse));double first=box[2]/slope,last=box[5]/slope;if(first>last)std::swap(first,last);
            if(last < -splitLineTolerance)continue;
            if(first < -splitLineTolerance)throw std::runtime_error("Split-line projection crosses the sketch plane; choose an exterior sketch and unambiguous forward direction");
            candidates.push_back({wire,first,last});
        }
    }
    if(candidates.empty())throw std::runtime_error("Split-line sketch has no forward intersection with the selected face");
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    // A cylindrical face may project twice. The nearest forward branch is
    // accepted only when exact curve bounds prove it precedes every other
    // branch over the whole wire. Overlapping/ambiguous depth intervals reject.
    for(size_t i=1;i<candidates.size();++i)if(candidates[0].last+splitLineTolerance>=candidates[i].first)throw std::runtime_error("Split-line projection has overlapping or disconnected branches; use a smaller connected sketch or target face");
    const auto projected=candidates.front().wire;splitTolerance(projected,"Split-line projection");
    BRepFeat_SplitShape split(input);split.SetCheckInterior(true);split.Add(projected,target);split.Build();
    if(!split.IsDone())throw std::runtime_error("Split-line exact face partition failed");const auto result=split.Shape();shapeReport(result,true);splitTolerance(result,"Split-line result");
    std::vector<TopoDS_Shape> regions;for(TopTools_ListIteratorOfListOfShape it(split.Modified(target));it.More();it.Next()){
        if(it.Value().ShapeType()!=TopAbs_FACE)continue;bool duplicate=false;for(const auto& prior:regions)duplicate|=prior.IsSame(it.Value());if(!duplicate)regions.push_back(it.Value());
    }
    if(regions.size()<2||regions.size()>64)throw std::runtime_error("Split-line projection does not partition the selected face into 2..64 regions; an open curve must join face boundaries");
    if(shapes(result,TopAbs_FACE).size()!=shapes(input,TopAbs_FACE).size()+regions.size()-1)throw std::runtime_error("Split line altered faces outside the selected exact face");
    const double before=properties(input),after=properties(result),volumeTolerance=std::max(1e-6,std::abs(before)*1e-8),difference=symmetricDifference(input,result);
    const double beforeArea=splitArea(input),afterArea=splitArea(result),areaTolerance=std::max(1e-6,std::abs(beforeArea)*1e-8);
    if(std::abs(before-after)>volumeTolerance||difference>volumeTolerance||std::abs(beforeArea-afterArea)>areaTolerance)throw std::runtime_error("Split line changed the source material volume or boundary area: volume delta="+std::to_string(after-before)+", material difference="+std::to_string(difference)+", area delta="+std::to_string(afterArea-beforeArea)+", volume limit="+std::to_string(volumeTolerance)+", area limit="+std::to_string(areaTolerance));
    const auto beforeBox=bounds(input),afterBox=bounds(result);double boundError=0;for(size_t i=0;i<6;++i)boundError=std::max(boundError,std::abs(beforeBox[i]-afterBox[i]));if(boundError>splitLineTolerance)throw std::runtime_error("Split line changed the source solid bounds");
    // Every published region retains the original exact supporting surface.
    TopLoc_Location originalLocation;const auto originalSurface=BRep_Tool::Surface(target,originalLocation);double regionArea=0;
    for(const auto& region:regions){TopLoc_Location location;if(BRep_Tool::Surface(TopoDS::Face(region),location)!=originalSurface||!location.IsEqual(originalLocation))throw std::runtime_error("Split line replaced the selected geometric support");const auto area=splitArea(region);if(area<=1e-12)throw std::runtime_error("Split line produced a zero-area region");regionArea+=area;}
    if(std::abs(regionArea-splitArea(target))>areaTolerance)throw std::runtime_error("Split-line region areas do not conserve the selected face");
    std::sort(regions.begin(),regions.end(),[](const auto& a,const auto& b){return splitRegionOrder(a)<splitRegionOrder(b);});
    const auto prefix="split."+sourceHash.substr(0,12);V regionNames=V::makeArr(),regionRows=V::makeArr();
    refs=propagated(refs,result,split);
    for(auto& ref:refs)if(ref.name==faceName){ref.meta=V::makeObj();ref.meta.set("reason",V::makeStr("selected exact face partitioned by a projected sketch wire"));}
    for(size_t i=0;i<regions.size();++i){const auto name=prefix+".region."+std::to_string(i+1);add(refs,name,"face",regions[i]);regionNames.arr.push_back(V::makeStr(name));V row=V::makeObj();const auto order=splitRegionOrder(regions[i]);row.set("reference",V::makeStr(name));row.set("area_mm2",V::makeNum(order[3]));row.set("center_mm",vec(gp_XYZ(order[0],order[1],order[2])));regionRows.arr.push_back(row);}
    for(auto& ref:refs)if(ref.name==faceName)ref.meta.set("split_into",regionNames);
    TopTools_IndexedDataMapOfShapeListOfShape adjacency;TopExp::MapShapesAndAncestors(result,TopAbs_EDGE,TopAbs_FACE,adjacency);V boundaryNames=V::makeArr();
    for(int i=1;i<=adjacency.Extent();++i){std::set<size_t> neighbors;for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(i));it.More();it.Next())for(size_t region=0;region<regions.size();++region)if(it.Value().IsSame(regions[region]))neighbors.insert(region);
        if(neighbors.size()<2)continue;const auto name=prefix+".boundary."+std::to_string(boundaryNames.arr.size()+1);add(refs,name,"edge",adjacency.FindKey(i));boundaryNames.arr.push_back(V::makeStr(name));
    }
    if(boundaryNames.arr.empty())throw std::runtime_error("Split line produced no exact shared region boundary");
    receipt=V::makeObj();receipt.set("source_face",V::makeStr(faceName));receipt.set("source_topology",V::makeStr(sourceHash));receipt.set("geometry_authority",V::makeStr("exact_brep"));receipt.set("projection_mode",V::makeStr("nearest_forward_directional_branch"));receipt.set("projection_representation",V::makeStr(representation));receipt.set("direction",direction(projectionDirection));receipt.set("projected_wire_brep_base64",V::makeStr(brepData(projected)));receipt.set("projection_branch_count",V::makeNum(candidates.size()));receipt.set("regions",regionRows);receipt.set("boundary_references",boundaryNames);receipt.set("same_solid_preserved",V::makeBool(true));receipt.set("support_surface_preserved",V::makeBool(true));receipt.set("source_volume_mm3",V::makeNum(before));receipt.set("volume_delta_mm3",V::makeNum(after-before));receipt.set("symmetric_difference_mm3",V::makeNum(difference));receipt.set("volume_tolerance_mm3",V::makeNum(volumeTolerance));receipt.set("area_delta_mm2",V::makeNum(afterArea-beforeArea));receipt.set("result_area_mm2",V::makeNum(afterArea));receipt.set("area_integration_relative_precision",V::makeNum(1e-12));receipt.set("tolerance_mm",V::makeNum(splitLineTolerance));receipt.set("manufacturing_qualified",V::makeBool(false));return result;
}
} // private split-line helpers
