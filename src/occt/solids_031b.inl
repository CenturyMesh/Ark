// Exact, bounded solid recipes and edge/split operations (B.02/B.03).
// Included after the common solid/topology helpers; never consumes meshes.
namespace {
bool solidRecipe031b(const std::string& op){
    return op=="rib"||op=="gusset"||op=="boss"||op=="mount_pad"||op=="screw_support"||op=="nut_seat"||op=="lid_lip"||op=="lid_groove";
}
const Ref& face031b(const std::vector<Ref>& refs,const std::string& name,const char* role){
    const Ref* found=nullptr;for(const auto& ref:refs)if(ref.name==name){if(found)throw std::runtime_error(std::string(role)+" face is ambiguous");found=&ref;}
    if(!found||found->kind!="face"||!selectable(found->status)||found->shape.IsNull())throw std::runtime_error(std::string(role)+" face is missing, obsolete, split or deleted: "+name);
    return *found;
}
gp_Trsf support031b(const V& request,const std::vector<Ref>& refs){
    const auto& ref=face031b(refs,word(request,"source_face"),"Recipe support");
    if(const auto* identity=request.find("source_topology"))if(identity->t!=V::Str||identity->str!=digest(word(request,"input_brep")))throw std::runtime_error("Recipe source topology changed; inspect the current exact face");
    BRepAdaptor_Surface surface(TopoDS::Face(ref.shape));if(surface.GetType()!=GeomAbs_Plane)throw std::runtime_error("Solid recipe requires an exact planar support face");
    auto plane=surface.Plane();gp_Dir normal=plane.Axis().Direction();if(ref.shape.Orientation()==TopAbs_REVERSED)normal.Reverse();
    gp_Dir x=plane.XAxis().Direction();gp_Dir y=normal.Crossed(x);const auto origin=plane.Location().Translated(gp_Vec(x)*optional(request,"cx",0)+gp_Vec(y)*optional(request,"cy",0));
    gp_Trsf placement;placement.SetValues(x.X(),y.X(),normal.X(),origin.X(),x.Y(),y.Y(),normal.Y(),origin.Y(),x.Z(),y.Z(),normal.Z(),origin.Z());return placement;
}
void recipeFields031b(const V& request,const std::string& op){
    std::set<std::string> allowed{"op","input_brep","input_topology","source_face","source_topology","cx","cy"};
    if(op=="rib"||op=="gusset")for(const char* k:{"length","thickness","height"})allowed.insert(k);
    if(op=="mount_pad")for(const char* k:{"width","depth","height"})allowed.insert(k);
    if(op=="boss"||op=="screw_support")for(const char* k:{"radius","height","bore_radius"})allowed.insert(k);
    if(op=="nut_seat")for(const char* k:{"across_flats","depth","angle","bore_radius","bore_depth"})allowed.insert(k);
    if(op=="lid_lip"||op=="lid_groove")for(const char* k:{"width","depth","wall","height","groove_depth"})allowed.insert(k);
    for(const auto& field:request.obj)if(!allowed.count(field.first))throw std::runtime_error("Unknown "+op+" recipe field: "+field.first);
}
V modelingRecipe031b(const V& request){
    const auto op=word(request,"op");recipeFields031b(request,op);const auto input=readBrep(word(request,"input_brep"));shapeReport(input,true);requireOneSolid(input,"Solid recipe");
    auto refs=inputRefs(request,"input_brep","input_topology",input);const auto placement=support031b(request,refs);TopoDS_Shape tool;std::vector<Ref> toolRefs;const bool subtract=op=="nut_seat"||op=="lid_groove";
    if(op=="rib"||op=="mount_pad"){
        const double x=positive(request,op=="rib"?"length":"width"),y=positive(request,op=="rib"?"thickness":"depth"),h=positive(request,"height");
        BRepPrimAPI_MakeBox maker(gp_Pnt(-x/2,-y/2,0),x,y,h);tool=maker.Shape();add(toolRefs,op+".top","face",maker.TopFace());add(toolRefs,op+".base","face",maker.BottomFace());add(toolRefs,op+".x_min","face",maker.BackFace());add(toolRefs,op+".x_max","face",maker.FrontFace());add(toolRefs,op+".y_min","face",maker.LeftFace());add(toolRefs,op+".y_max","face",maker.RightFace());
    }else if(op=="gusset"){
        const double length=positive(request,"length"),thickness=positive(request,"thickness"),height=positive(request,"height");BRepBuilderAPI_MakePolygon polygon;
        polygon.Add(gp_Pnt(0,-thickness/2,0));polygon.Add(gp_Pnt(length,-thickness/2,0));polygon.Add(gp_Pnt(0,-thickness/2,height));polygon.Close();
        if(!polygon.IsDone())throw std::runtime_error("Gusset exact triangle failed");BRepBuilderAPI_MakeFace face(polygon.Wire());if(!face.IsDone())throw std::runtime_error("Gusset planar profile failed");
        BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(0,thickness,0));prism.Build();if(!prism.IsDone())throw std::runtime_error("Gusset exact prism failed");tool=prism.Shape();add(toolRefs,"gusset.side.start","face",prism.FirstShape());add(toolRefs,"gusset.side.end","face",prism.LastShape());
    }else if(op=="boss"||op=="screw_support"){
        const double radius=positive(request,"radius"),height=positive(request,"height"),bore=optional(request,"bore_radius",0);
        if(bore<0||bore>=radius||(op=="screw_support"&&bore<=0))throw std::runtime_error("Boss bore_radius must be nonnegative and smaller than radius; screw_support requires a positive bore");
        tool=BRepPrimAPI_MakeCylinder(radius,height).Shape();axialFaces(tool,toolRefs,true);for(auto& ref:toolRefs)ref.name=op+"."+ref.name;
        if(bore>0){const auto cutter=BRepPrimAPI_MakeCylinder(bore,height).Shape();BRepAlgoAPI_Cut cut(tool,cutter);cut.Build();if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Boss exact bore failed");tool=cut.Shape();toolRefs=propagated(toolRefs,tool,cut);holesSemanticRefs(tool,gp_Pnt(0,0,0),gp_Dir(0,0,1),bore,op+".bore",toolRefs);}
    }else if(op=="nut_seat"){
        const double flats=positive(request,"across_flats"),depth=positive(request,"depth"),angle=optional(request,"angle",0)*std::acos(-1.)/180.;const double radius=flats/std::sqrt(3.);
        BRepBuilderAPI_MakePolygon polygon;for(int i=0;i<6;++i){double a=angle+i*std::acos(-1.)/3;polygon.Add(gp_Pnt(radius*std::cos(a),radius*std::sin(a),0));}polygon.Close();
        BRepBuilderAPI_MakeFace face(polygon.Wire());if(!face.IsDone())throw std::runtime_error("Nut seat exact hexagon failed");tool=BRepPrimAPI_MakePrism(face.Face(),gp_Vec(0,0,-depth)).Shape();
        const double bore=optional(request,"bore_radius",0);if(bore<0||bore>=flats/2)throw std::runtime_error("Nut seat bore_radius must be nonnegative and smaller than half across_flats");
        if(bore>0){const double boreDepth=positive(request,"bore_depth");if(boreDepth<=depth)throw std::runtime_error("Nut seat bore_depth must exceed seat depth");const auto cylinder=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,0,-1)),bore,boreDepth).Shape();BRepAlgoAPI_Fuse fuse(tool,cylinder);fuse.Build();if(!fuse.IsDone()||fuse.HasErrors())throw std::runtime_error("Nut seat exact clearance tool failed");tool=fuse.Shape();}
        else if(request.find("bore_depth"))throw std::runtime_error("Nut seat bore_depth requires bore_radius");
        for(const auto& f:shapes(tool,TopAbs_FACE)){BRepAdaptor_Surface s(TopoDS::Face(f));if(s.GetType()==GeomAbs_Plane&&std::abs(s.Plane().Location().Z()+depth)<1e-7)add(toolRefs,"nut_seat.floor","face",f);}
    }else{
        const double x=positive(request,"width"),y=positive(request,"depth"),wall=positive(request,"wall"),h=positive(request,subtract?"groove_depth":"height");if(2*wall>=std::min(x,y))throw std::runtime_error("Lid ring wall must leave a positive rectangular opening");
        if(op=="lid_lip"&&request.find("groove_depth"))throw std::runtime_error("Lid lip uses height only");if(op=="lid_groove"&&request.find("height"))throw std::runtime_error("Lid groove uses groove_depth only");const double z=subtract?-h:0;
        const auto outer=BRepPrimAPI_MakeBox(gp_Pnt(-x/2,-y/2,z),x,y,h).Shape(),inner=BRepPrimAPI_MakeBox(gp_Pnt(-x/2+wall,-y/2+wall,z),x-2*wall,y-2*wall,h).Shape();
        BRepAlgoAPI_Cut cut(outer,inner);cut.Build();if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Lid exact rectangular ring failed");tool=cut.Shape();
        int side=0;for(const auto& f:shapes(tool,TopAbs_FACE))add(toolRefs,op+".face."+std::to_string(side++),"face",f);
    }
    std::string nameSpace=op;for(int ordinal=2;;++ordinal){bool occupied=false;for(const auto& ref:refs)occupied|=ref.name.rfind(nameSpace+".",0)==0;if(!occupied)break;nameSpace=op+std::to_string(ordinal);}if(nameSpace!=op)for(auto& ref:toolRefs)if(ref.name.rfind(op+".",0)==0)ref.name=nameSpace+ref.name.substr(op.size());
    shapeReport(tool,true);BRepBuilderAPI_Transform move(tool,placement,true);if(!move.IsDone())throw std::runtime_error("Solid recipe support placement failed");toolRefs=propagated(toolRefs,move.Shape(),move);tool=move.Shape();
    BRepAlgoAPI_Common supportContact(tool,face031b(refs,word(request,"source_face"),"Recipe support").shape);supportContact.Build();if(!supportContact.IsDone()||supportContact.HasErrors())throw std::runtime_error("Solid recipe exact support contact check failed");GProp_GProps contactArea;BRepGProp::SurfaceProperties(supportContact.Shape(),contactArea);if(!std::isfinite(contactArea.Mass())||contactArea.Mass()<=1e-8)throw std::runtime_error("Solid recipe footprint must overlap a positive area of the selected support face");
    const double before=properties(input),toolVolume=properties(tool),volumeTol=std::max(1e-6,toolVolume*1e-8);TopoDS_Shape result;
    if(subtract){BRepAlgoAPI_Cut algorithm(input,tool);algorithm.Build();if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("Solid recipe exact subtraction failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);auto produced=propagated(toolRefs,result,algorithm);refs.insert(refs.end(),produced.begin(),produced.end());
        const double removed=before-properties(result);if(std::abs(removed-toolVolume)>volumeTol)throw std::runtime_error("Seat/groove tool must be fully contained in source stock; breakout or inactive cut: removed="+std::to_string(removed)+", tool="+std::to_string(toolVolume));
    }else{BRepAlgoAPI_Fuse algorithm(input,tool);algorithm.Build();if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("Solid recipe exact union failed");result=algorithm.Shape();refs=propagated(refs,result,algorithm);auto produced=propagated(toolRefs,result,algorithm);refs.insert(refs.end(),produced.begin(),produced.end());}
    requireOneSolid(result,"Recipe result (tool must join source)");shapeReport(result,true);const double after=properties(result),change=subtract?before-after:after-before;if(change<=volumeTol)throw std::runtime_error("Solid recipe must change a positive measured material volume");
    V output=finish(result,refs),receipt=V::makeObj(),report=need(output,"report");receipt.set("operation",V::makeStr(op));receipt.set("reference_namespace",V::makeStr(nameSpace));receipt.set("geometry_authority",V::makeStr("exact_brep"));receipt.set("source_face",need(request,"source_face"));receipt.set("support_frame",matrix(placement));receipt.set("source_volume_mm3",V::makeNum(before));receipt.set("tool_volume_mm3",V::makeNum(toolVolume));receipt.set("result_volume_mm3",V::makeNum(after));receipt.set("material_change_mm3",V::makeNum(change));receipt.set("subtractive",V::makeBool(subtract));receipt.set("complete_cut_tool_contained",V::makeBool(subtract));receipt.set("boolean_tolerance_changed",V::makeBool(false));receipt.set("manufacturing_qualified",V::makeBool(false));report.set("solid_recipe",receipt);output.set("report",report);return output;
}

TopoDS_Shape finishEdges031b(const TopoDS_Shape& input,const V& request,std::vector<Ref>& refs,V& receipt){
    requireOneSolid(input,"Fillet/chamfer");const auto op=word(request,"op");const auto edges=chosenEdges(input,request,refs);TopoDS_Shape result;receipt=V::makeObj();receipt.set("geometry_authority",V::makeStr("exact_brep"));receipt.set("edge_count",V::makeNum(edges.size()));receipt.set("boolean_tolerance_changed",V::makeBool(false));
    if(op=="fillet"){
        BRepFilletAPI_MakeFillet algorithm(input);
        if(const auto* law=request.find("radius_law")){
            if(request.find("radius"))throw std::runtime_error("Variable fillet requires radius_law only, without inactive radius");
            if(edges.size()!=1||!request.find("edges")||BRepAdaptor_Curve(edges[0]).GetType()!=GeomAbs_Line)throw std::runtime_error("Variable fillet supports one explicitly named straight edge");
            if(law->t!=V::Arr||law->arr.size()<2||law->arr.size()>16)throw std::runtime_error("Variable fillet radius_law needs 2..16 fraction/radius stations");NCollection_Array1<gp_Pnt2d> stations(1,(int)law->arr.size());double previous=-1;
            for(size_t i=0;i<law->arr.size();++i){const auto& station=law->arr[i];if(station.t!=V::Obj||station.obj.size()!=2||!station.find("fraction")||!station.find("radius"))throw std::runtime_error("Variable fillet stations require only fraction and radius");const double fraction=scalar(station,"fraction"),radius=positive(station,"radius");if(fraction<0||fraction>1||fraction<=previous)throw std::runtime_error("Variable fillet fractions must increase in [0,1]");previous=fraction;stations.SetValue((int)i+1,gp_Pnt2d(fraction,radius));}
            if(stations.First().X()!=0||stations.Last().X()!=1)throw std::runtime_error("Variable fillet radius_law must include fractions 0 and 1");
            // OCCT contour parameter increases from the edge's actual first
            // endpoint. Reverse authored fractions if the semantic tangent is
            // opposed, preserving the producer's directed semantic edge.
            BRepAdaptor_Curve curve(edges[0]);gp_Pnt p;gp_Vec tangent;curve.D1(curve.FirstParameter(),p,tangent);const auto& selected=need(request,"edges").arr[0].str;for(const auto& ref:refs)if(ref.name==selected){const auto d=descriptor(ref);const auto v=point(need(d,"tangent"),"semantic tangent");if(tangent.Dot(gp_Vec(v.X(),v.Y(),v.Z()))<0){NCollection_Array1<gp_Pnt2d> reversed(1,stations.Size());for(int i=1;i<=stations.Size();++i){const auto q=stations.Value(stations.Size()+1-i);reversed.SetValue(i,gp_Pnt2d(1-q.X(),q.Y()));}stations=reversed;}break;}
            algorithm.Add(stations,edges[0]);if(algorithm.NbContours()!=1||algorithm.NbEdges(1)!=1)throw std::runtime_error("Variable fillet straight edge must form one isolated contour; tangent chains are outside current domain");receipt.set("mode",V::makeStr("variable_radius"));receipt.set("radius_law",*law);receipt.set("fraction_parameter",V::makeStr("semantic_straight_edge_direction"));
        }else{const double radius=positive(request,"radius");for(const auto& edge:edges)algorithm.Add(radius,edge);receipt.set("mode",V::makeStr("constant_radius"));}
        algorithm.Build();if(!algorithm.IsDone())throw std::runtime_error("Native fillet failed within requested radii; no radius/tolerance fallback");result=algorithm.Shape();refs=propagated(refs,result,algorithm);
    }else{
        const double distance=positive(request,"distance");const bool legacyEqual=request.find("distance2")&&!request.find("reference_face")&&std::abs(positive(request,"distance2")-distance)<=1e-12;const std::string mode=request.find("mode")?word(request,"mode"):(request.find("angle")?"distance_angle":request.find("distance2")&&!legacyEqual?"distance_distance":"symmetric");if(mode!="symmetric"&&mode!="distance_distance"&&mode!="distance_angle")throw std::runtime_error("Chamfer mode must be symmetric, distance_distance or distance_angle");
        if(mode=="distance_angle"&&request.find("distance2"))throw std::runtime_error("Angle chamfer cannot retain distance2");if(mode!="distance_angle"&&request.find("angle"))throw std::runtime_error("Chamfer angle requires distance_angle mode");
        BRepFilletAPI_MakeChamfer algorithm(input);if(mode=="symmetric"){if(request.find("reference_face"))throw std::runtime_error("Symmetric chamfer does not use reference_face");if(request.find("distance2")&&std::abs(positive(request,"distance2")-distance)>1e-12)throw std::runtime_error("Symmetric chamfer distance2 differs");for(const auto& edge:edges)algorithm.Add(distance,edge);}
        else{const auto face=TopoDS::Face(face031b(refs,word(request,"reference_face"),"Chamfer reference").shape);for(const auto& edge:edges){bool adjacent=false;for(const auto& boundary:shapes(face,TopAbs_EDGE))adjacent|=boundary.IsSame(edge);if(!adjacent)throw std::runtime_error("Chamfer reference_face must be adjacent to every selected edge");if(mode=="distance_distance")algorithm.Add(distance,positive(request,"distance2"),edge,face);else{const double angle=positive(request,"angle");if(angle>=90)throw std::runtime_error("Chamfer angle must lie strictly between 0 and 90 degrees");algorithm.AddDA(distance,angle*std::acos(-1.)/180.,edge,face);}}}
        algorithm.Build();if(!algorithm.IsDone())throw std::runtime_error("Native chamfer failed for explicit distances/angle; no fallback");result=algorithm.Shape();refs=propagated(refs,result,algorithm);receipt.set("mode",V::makeStr(mode));
    }
    requireOneSolid(result,"Edge finish result");shapeReport(result,true);receipt.set("manufacturing_qualified",V::makeBool(false));return result;
}

std::array<double,4> pieceOrder031b(const TopoDS_Shape& shape){GProp_GProps p;volumeCentroidProperties(shape,p);const auto c=p.CentreOfMass();return {c.X(),c.Y(),c.Z(),p.Mass()};}
template<class Algorithm>std::vector<Ref> splitLineage031b(const std::vector<Ref>& source,const std::vector<TopoDS_Shape>& pieces,const std::set<size_t>& retained,Algorithm& algorithm,const std::string& nameSpace){
    std::vector<Ref> result;
    for(auto ref:source){if(ref.name.find("@ark.")!=std::string::npos)continue;if(!selectable(ref.status)){result.push_back(ref);continue;}std::vector<Ref> children;V names=V::makeArr();
        for(size_t part:retained){const auto& piece=pieces[part];std::vector<TopoDS_Shape> matches;auto collect=[&](const TopTools_ListOfShape& list){for(TopTools_ListIteratorOfListOfShape it(list);it.More();it.Next())if(indexOf(piece,it.Value(),refType(ref.kind))>=0){bool duplicate=false;for(const auto& prior:matches)duplicate|=prior.IsSame(it.Value());if(!duplicate)matches.push_back(it.Value());}};collect(algorithm.Modified(ref.shape));collect(algorithm.Generated(ref.shape));if(matches.empty()&&indexOf(piece,ref.shape,refType(ref.kind))>=0)matches.push_back(ref.shape);
            for(size_t i=0;i<matches.size();++i){Ref child=ref;child.name=nameSpace+"."+std::to_string(part)+"."+ref.name+(matches.size()>1?"."+std::to_string(i):"");child.shape=matches[i];child.status="bound";child.meta=V::makeObj();children.push_back(child);names.arr.push_back(V::makeStr(child.name));}
        }
        if(children.size()==1){ref.shape=children[0].shape;ref.status="bound";}else{ref.shape.Nullify();ref.status=children.empty()?"deleted":"split";}if(!names.arr.empty())ref.meta.set("split_into",names);result.push_back(ref);result.insert(result.end(),children.begin(),children.end());
        if(result.size()>512)throw std::runtime_error("Split lineage exceeds 512 semantic references; reduce partitions/source reference scope");
    }return result;
}
TopoDS_Shape splitExact031b(const TopoDS_Shape& input,const V& request,std::vector<Ref>& refs,V& receipt){
    requireOneSolid(input,"Exact split source");const auto op=word(request,"op");const auto tool=readBrep(word(request,"tool_brep"));TopoDS_Shape cutter=tool;
    if(op=="split_surface"){const auto toolRefs=inputRefs(request,"tool_brep","tool_topology",tool);cutter=face031b(toolRefs,word(request,"tool_face"),"Split tool").shape;}
    else{requireOneSolid(tool,"Split tool");shapeReport(tool,true);if(request.find("tool_face"))throw std::runtime_error("split_tool uses the complete exact solid; tool_face requires split_surface");}
    BRepCheck_Analyzer check(cutter,true);if(!check.IsValid())throw std::runtime_error("Split exact tool/surface is invalid");
    TopTools_ListOfShape arguments,tools;arguments.Append(input);tools.Append(cutter);BRepAlgoAPI_Splitter algorithm;algorithm.SetArguments(arguments);algorithm.SetTools(tools);algorithm.SetNonDestructive(true);algorithm.Build();if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("Exact split algorithm failed without tolerance fallback");
    auto pieces=shapes(algorithm.Shape(),TopAbs_SOLID);if(pieces.size()<2||pieces.size()>64)throw std::runtime_error("Exact tool/surface must partition source into 2..64 positive solids; finite trimmed surfaces must span the section");
    std::sort(pieces.begin(),pieces.end(),[](const auto& a,const auto& b){return pieceOrder031b(a)<pieceOrder031b(b);});double sum=0;for(const auto& piece:pieces){shapeReport(piece,true);const double volume=properties(piece);if(!std::isfinite(volume)||volume<=1e-8)throw std::runtime_error("Exact split contains an invalid/zero volume piece; no pieces discarded");sum+=volume;}
    const double source=properties(input),tolerance=std::max(1e-5,source*1e-7);if(std::abs(sum-source)>tolerance)throw std::runtime_error("Exact split does not conserve source volume: source="+std::to_string(source)+", pieces="+std::to_string(sum)+", error="+std::to_string(sum-source)+", limit="+std::to_string(tolerance));
    const auto& keep=need(request,"retain");if(keep.t!=V::Arr||keep.arr.empty()||keep.arr.size()>pieces.size())throw std::runtime_error("Exact split retain must explicitly list 1..piece_count integer piece IDs");std::set<size_t> retained;for(const auto& id:keep.arr){if(id.t!=V::Num||id.num!=std::floor(id.num)||id.num<0||id.num>=pieces.size()||!retained.insert((size_t)id.num).second)throw std::runtime_error("Exact split retain has an invalid, duplicate or stale piece ID");}
    std::vector<TopoDS_Shape> selected;double keptVolume=0;V rows=V::makeArr();for(size_t i=0;i<pieces.size();++i){const auto order=pieceOrder031b(pieces[i]);V row=V::makeObj();row.set("id",V::makeNum(i));row.set("retained",V::makeBool(retained.count(i)!=0));row.set("volume_mm3",V::makeNum(order[3]));row.set("center_mm",vec(gp_XYZ(order[0],order[1],order[2])));V box=V::makeArr();for(double n:bounds(pieces[i]))box.arr.push_back(V::makeNum(n));row.set("bounds_mm",box);rows.arr.push_back(row);if(retained.count(i)){selected.push_back(pieces[i]);keptVolume+=order[3];}}
    std::string nameSpace="piece";for(int ordinal=2;;++ordinal){bool occupied=false;for(const auto& ref:refs)occupied|=ref.name.rfind(nameSpace+".",0)==0;if(!occupied)break;nameSpace="piece"+std::to_string(ordinal);}
    refs=splitLineage031b(refs,pieces,retained,algorithm,nameSpace);receipt=V::makeObj();receipt.set("reference_namespace",V::makeStr(nameSpace));receipt.set("geometry_authority",V::makeStr("exact_brep"));receipt.set("mode",V::makeStr(op));receipt.set("piece_order",V::makeStr("lexicographic_exact_volume_centroid_xyz_then_volume"));receipt.set("pieces",rows);receipt.set("source_volume_mm3",V::makeNum(source));receipt.set("partition_volume_mm3",V::makeNum(sum));receipt.set("retained_volume_mm3",V::makeNum(keptVolume));receipt.set("explicitly_removed_volume_mm3",V::makeNum(sum-keptVolume));receipt.set("volume_error_mm3",V::makeNum(sum-source));receipt.set("volume_tolerance_mm3",V::makeNum(tolerance));receipt.set("boolean_tolerance_changed",V::makeBool(false));receipt.set("manufacturing_qualified",V::makeBool(false));return selected.size()==1?selected.front():compound(selected);
}
TopoDS_Shape splitOnPlane031b(const TopoDS_Shape& input,const V& request,std::vector<Ref>& refs,V& receipt){
    const double before=properties(input);auto result=splitOnPlane(input,request,refs,&receipt);const double after=properties(result);receipt.set("mode",V::makeStr("split_plane"));receipt.set("geometry_authority",V::makeStr("exact_brep"));receipt.set("keep",need(request,"keep"));receipt.set("source_volume_mm3",V::makeNum(before));receipt.set("retained_volume_mm3",V::makeNum(after));receipt.set("explicitly_removed_volume_mm3",V::makeNum(before-after));receipt.set("boolean_tolerance_changed",V::makeBool(false));receipt.set("manufacturing_qualified",V::makeBool(false));return result;
}
} // private 031B helpers
