// A saved B-rep is explicit CAD state, not evidence that its recipe was replayed.
// This bounded reader rebuilds every currently authoritative geometric field.
// Included privately by common.cpp; no unchecked shape overload is public.

namespace snapshot031b {
using V=arkjson::Value;
[[noreturn]] void reject(const char* reason){throw std::runtime_error(std::string("Saved native result rejected: ")+reason);}
const V& require(const V& object,const char* name,V::Type type){const auto* value=object.find(name);if(!value||value->t!=type)reject(name);return *value;}
V* edit(V& object,const char* name){for(auto& item:object.obj)if(item.first==name)return &item.second;return nullptr;}
bool nameValid(const std::string& name){return !name.empty()&&name.size()<=512&&std::none_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;});}
void objectUnique(const V& object){if(object.t!=V::Obj)reject("expected object");std::set<std::string> keys;for(const auto& item:object.obj)if(!keys.insert(item.first).second)reject("duplicate object field");}
gp_XYZ xyz(const V& value){if(value.t!=V::Arr||value.arr.size()!=3)reject("reference vector must have three coordinates");for(const auto& x:value.arr)if(x.t!=V::Num||!std::isfinite(x.num))reject("non-finite reference vector");return {value.arr[0].num,value.arr[1].num,value.arr[2].num};}
gp_XYZ unit(const V& value){const auto direction=xyz(value);if(std::abs(direction.Modulus()-1)>1e-10)reject("reference direction is not unit length");return direction;}
bool selected(const std::string& status){return status=="bound"||status=="merged";}
void validateDescriptorTypes(const V& descriptor){objectUnique(descriptor);for(const auto& item:descriptor.obj){
    if(item.first=="origin")xyz(item.second);
    else if(item.first=="normal"||item.first=="axis"||item.first=="tangent"||item.first=="x_axis")unit(item.second);
    else if(item.first=="radius"){if(item.second.t!=V::Num||!std::isfinite(item.second.num)||item.second.num<=0)reject("invalid reference radius");}
    else if(item.first=="surface_type"){if(item.second.t!=V::Str||(item.second.str!="non_planar"&&item.second.str!="non_analytic"))reject("invalid reference surface type");}
    else reject("unknown reference descriptor field");
}}
// Reuse only the sign of a genuinely parallel stored datum direction.
gp_Dir signedDirection(gp_Dir actual,const V* descriptor,const char* key){if(descriptor)if(const auto* prior=descriptor->find(key)){const double dot=actual.XYZ().Dot(unit(*prior));if(std::abs(std::abs(dot)-1)>1e-10)reject("reference direction disagrees with exact geometry");if(dot<0)actual.Reverse();}return actual;}
V descriptor(const TopoDS_Shape& entity,const std::string& kind,const V* previous){
    V out=V::makeObj();if(previous)validateDescriptorTypes(*previous);
    if(kind=="edge"){
        const auto edge=TopoDS::Edge(entity);if(BRep_Tool::Degenerated(edge))return out;
        BRepAdaptor_Curve curve(edge);const double first=curve.FirstParameter(),last=curve.LastParameter();if(!std::isfinite(first)||!std::isfinite(last))reject("unbounded reference edge");gp_Pnt origin;gp_Vec tangent;curve.D1(first+(last-first)*.5,origin,tangent);if(tangent.Magnitude()<=1e-12)reject("degenerate reference tangent");
        auto direction=signedDirection(gp_Dir(tangent),previous,"tangent");out.set("origin",vec(origin.XYZ()));out.set("tangent",vec(direction.XYZ()));if(curve.GetType()==GeomAbs_Circle)out.set("radius",V::makeNum(curve.Circle().Radius()));return out;
    }
    BRepAdaptor_Surface surface(TopoDS::Face(entity));
    if(surface.GetType()==GeomAbs_Plane){
        const auto plane=surface.Plane();auto normal=plane.Axis().Direction();if(entity.Orientation()==TopAbs_REVERSED)normal.Reverse();
        if(kind=="axis"){
            // An explicit planar datum is saved design state. Its origin must
            // attach to this support plane; it cannot assert a remote frame.
            if(!previous||!previous->find("axis")||!previous->find("origin"))reject("planar axis requires an explicit attached datum");const auto origin=xyz(*previous->find("origin")),axis=unit(*previous->find("axis"));if(std::abs((origin-plane.Location().XYZ()).Dot(normal.XYZ()))>1e-7)reject("planar datum origin is off its support plane");out.set("origin",vec(origin));out.set("axis",vec(axis));return out;
        }
        if(previous)if(const auto* prior=previous->find("normal"))if(normal.XYZ().Dot(unit(*prior))<1-1e-10)reject("saved face normal is not outward");
        auto origin=plane.Location().XYZ(),x=plane.XAxis().Direction().XYZ();
        if(previous){if(const auto* prior=previous->find("origin")){origin=xyz(*prior);if(std::abs((origin-plane.Location().XYZ()).Dot(normal.XYZ()))>1e-7)reject("face datum origin is off its support plane");}if(const auto* prior=previous->find("x_axis")){x=unit(*prior);if(std::abs(x.Dot(normal.XYZ()))>1e-10)reject("face datum x axis is not tangent");}}
        out.set("origin",vec(origin));out.set("normal",vec(normal.XYZ()));out.set("x_axis",vec(x));return out;
    }
    if(surface.GetType()==GeomAbs_Cylinder){const auto s=surface.Cylinder();out.set("origin",vec(s.Location().XYZ()));out.set("axis",vec(signedDirection(s.Axis().Direction(),previous,"axis").XYZ()));out.set("radius",V::makeNum(s.Radius()));}
    else if(surface.GetType()==GeomAbs_Cone){const auto s=surface.Cone();out.set("origin",vec(s.Location().XYZ()));out.set("axis",vec(signedDirection(s.Axis().Direction(),previous,"axis").XYZ()));}
    else if(surface.GetType()==GeomAbs_Sphere){const auto s=surface.Sphere();if(kind=="axis")reject("sphere has no unique axis reference");out.set("origin",vec(s.Location().XYZ()));out.set("radius",V::makeNum(s.Radius()));}
    else if(surface.GetType()==GeomAbs_Torus){const auto s=surface.Torus();out.set("origin",vec(s.Location().XYZ()));out.set("axis",vec(signedDirection(s.Axis().Direction(),previous,"axis").XYZ()));out.set("radius",V::makeNum(s.MajorRadius()));}
    else {if(kind=="axis")reject("surface has no validated analytic axis");out.set("surface_type",V::makeStr("non_analytic"));}
    return out;
}
void stringList(const V& value){if(value.t!=V::Arr||value.arr.size()>16384)reject("invalid reference lineage");std::set<std::string> names;for(const auto& item:value.arr)if(item.t!=V::Str||!nameValid(item.str)||!names.insert(item.str).second)reject("invalid reference lineage name");}
void validateStoredDescriptor(const V& prior,const V& rebuilt,const TopoDS_Shape& entity,const std::string& kind){
    // Absence is meaningful producer representation. Existing callers derive
    // frames from the exact entity; accepted stored fields must agree with it.
    validateDescriptorTypes(prior);
    if(const auto* radius=prior.find("radius")){const auto* actual=rebuilt.find("radius");if(!actual||std::abs(radius->num-actual->num)>std::max(1e-10,std::abs(actual->num)*1e-10))reject("saved reference radius disagrees with exact geometry");}
    if(const auto* origin=prior.find("origin")){const auto* actual=rebuilt.find("origin");if(!actual)reject("saved reference origin has no analytic support");
        if(kind=="edge"){if((xyz(*origin)-xyz(*actual)).Modulus()>1e-7)reject("saved edge origin disagrees with exact midpoint");}
        else {BRepAdaptor_Surface surface(TopoDS::Face(entity));if(surface.GetType()!=GeomAbs_Plane){const auto delta=xyz(*origin)-xyz(*actual);const auto* axis=rebuilt.find("axis");if(kind=="axis"&&axis){const auto direction=unit(*axis);if((delta-direction*delta.Dot(direction)).Modulus()>1e-7)reject("saved datum origin is off its analytic axis");}else if(delta.Modulus()>1e-7)reject("saved face origin disagrees with exact surface");}}
    }
    if(const auto* type=prior.find("surface_type")){if(kind=="edge")reject("edge has a surface descriptor");BRepAdaptor_Surface surface(TopoDS::Face(entity));if(surface.GetType()==GeomAbs_Plane||(type->str=="non_analytic"&&surface.GetType()!=GeomAbs_BSplineSurface&&surface.GetType()!=GeomAbs_BezierSurface&&surface.GetType()!=GeomAbs_SurfaceOfRevolution&&surface.GetType()!=GeomAbs_SurfaceOfExtrusion&&surface.GetType()!=GeomAbs_OffsetSurface&&surface.GetType()!=GeomAbs_OtherSurface))reject("saved surface type disagrees with exact geometry");}
}
V topology(const V& saved,const TopoDS_Shape& shape,const std::string& identity){
    objectUnique(saved);if(require(saved,"brep_sha256",V::Str).str!=identity)reject("B-rep digest mismatch");require(saved,"references",V::Arr);V out=saved;auto& rows=*edit(out,"references");if(rows.arr.size()>16384)reject("invalid topology references");
    if(!require(saved,"selection_inventory_complete",V::Bool).b||require(saved,"generic_reference_scope",V::Str).str!="current_brep_digest"||require(saved,"reference_capacity",V::Num).num!=16384)reject("saved selection completeness contract mismatch");
    const auto faces=shapes(shape,TopAbs_FACE),edges=shapes(shape,TopAbs_EDGE);std::set<std::string> names;size_t semantic=0;
    for(auto& row:rows.arr){objectUnique(row);const auto& name=require(row,"name",V::Str).str;const auto kind=require(row,"kind",V::Str).str,status=require(row,"status",V::Str).str;const auto& indices=require(row,"indices",V::Arr);
        if(!nameValid(name)||!names.insert(name).second)reject("invalid or duplicate reference name");if(kind!="face"&&kind!="edge"&&kind!="axis")reject("invalid reference kind");if(status!="bound"&&status!="merged"&&status!="split"&&status!="deleted"&&status!="obsolete")reject("invalid reference status");
        if(const auto marker=name.find("@ark.");marker==std::string::npos){if(++semantic>512)reject("semantic reference capacity exceeded");}
        else {const auto start=name.find('.',marker+5);const auto suffix=start==std::string::npos||name.size()<start+66?std::string():name.substr(start+66);const auto digest=start==std::string::npos?std::string():name.substr(start+1,64);
            if(start==std::string::npos||(kind!="face"&&kind!="edge")||name.substr(marker+5,start-marker-5)!=kind||digest.size()!=64||digest.find_first_not_of("0123456789abcdef")!=std::string::npos||suffix.empty()||suffix.size()>7||suffix.find_first_not_of("0123456789")!=std::string::npos||name[start+65]!='.')reject("invalid reserved reference provenance");
            if(const auto* declared=row.find("brep_sha256");declared&&(declared->t!=V::Str||declared->str!=digest))reject("reserved reference digest metadata mismatch");
            if(selected(status)&&(digest!=identity||indices.arr.size()!=1||indices.arr[0].t!=V::Num||indices.arr[0].num!=std::stoul(suffix)))reject("live reserved reference is stale or retargeted");
        }
        for(const char* key:{"merged_from","split_into"})if(const auto* value=row.find(key))stringList(*value);
        if(const auto* value=row.find("surviving")){if(value->t==V::Arr)stringList(*value);else if(value->t!=V::Str||!nameValid(value->str))reject("invalid surviving reference");}
        for(const char* key:{"reason","display_name","reference_scope","brep_sha256"})if(const auto* value=row.find(key);value&&(value->t!=V::Str||value->str.size()>1024))reject("invalid reference text metadata");
        for(const char* key:{"persistent","frame_available"})if(const auto* value=row.find(key);value&&value->t!=V::Bool)reject("invalid reference boolean metadata");
        if(selected(status)){const auto& entities=kind=="edge"?edges:faces;if(indices.arr.size()!=1||indices.arr[0].t!=V::Num||!std::isfinite(indices.arr[0].num)||indices.arr[0].num<0||std::floor(indices.arr[0].num)!=indices.arr[0].num||indices.arr[0].num>=entities.size())reject("reference index outside saved B-rep");
            const auto& entity=entities[(size_t)indices.arr[0].num];const V* previous=row.find("descriptor");V rebuilt=descriptor(entity,kind,previous);if(previous)validateStoredDescriptor(*previous,rebuilt,entity,kind);
        }else {if(!indices.arr.empty())reject("unbound reference contains indices");if(const auto* previous=row.find("descriptor");previous&&previous->t!=V::Null)validateDescriptorTypes(*previous);}
    }
    // Vertices are exact indexed entities, never mesh vertices. Partial saved
    // registries cannot silently become a complete current registry.
    if(const auto* vertices=saved.find("vertices")){
        const auto& complete=require(saved,"vertex_inventory_complete",V::Bool);const auto& scope=require(saved,"vertex_reference_scope",V::Str);if(scope.str!="current_exact_brep_digest"||vertices->t!=V::Arr)reject("invalid vertex inventory contract");NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> points;TopExp::MapShapes(shape,TopAbs_VERTEX,points);if(complete.b!=(points.Extent()<=16384)||vertices->arr.size()!=(complete.b?(size_t)points.Extent():0))reject("saved vertex inventory count mismatch");
        for(size_t i=0;i<vertices->arr.size();++i){const auto& prior=vertices->arr[i];objectUnique(prior);if(require(prior,"name",V::Str).str!="vertex."+std::to_string(i)||require(prior,"source_topology",V::Str).str!=identity||require(prior,"persistent",V::Bool).b)reject("saved vertex provenance mismatch");const auto actual=BRep_Tool::Pnt(TopoDS::Vertex(points((int)i+1))).XYZ();if((xyz(require(prior,"point_mm",V::Arr))-actual).Modulus()>1e-7)reject("saved vertex coordinates mismatch");}
    }else if(saved.find("vertex_inventory_complete")||saved.find("vertex_reference_scope"))reject("vertex inventory is missing");
    return out;
}
bool genericReportField(const std::string& key){static const std::set<std::string> keys{"kernel","kernel_version","brep_valid","solids","faces","area_mm2","volume_mm3","bounds_mm","display_mesh_authoritative","self_intersections_checked","manufacturing_authoritative","saved_previous_evaluation","derivation_replayed","snapshot_validation"};return keys.count(key)!=0;}
V restore(const V& resolved,const V& saved){
    NativeProfileScope profile("snapshot.restore");objectUnique(resolved);objectUnique(saved);const auto operation=require(resolved,"operation",V::Str).str;const auto& request=require(resolved,"request",V::Obj);const auto op=require(request,"op",V::Str).str;
    static const std::set<std::string> solidOps{"box","sphere","cylinder","cone","torus","extrude","union","cut","common","hole","holes_orchestrator","fillet","chamfer","transform","linear_pattern","circular_pattern","mirror","shell","draft","split_plane","curve_pattern","hole_pattern","rib","gusset","mount_pad","boss","screw_support","nut_seat","lid_lip","lid_groove","split_surface","split_tool"};
    static const std::set<std::string> surfaceOps{"surface_nurbs","extrude_surface","revolve_surface","sweep","loft","thicken","offset_surface","surface_extract_face","surface_trim_wire","surface_untrim","surface_extend"};
    const bool solid=operation=="model_solid";if(!(solid?solidOps.count(op):operation=="model"&&surfaceOps.count(op)))reject("operation requires normal derivation replay");
    const auto& encoded=require(saved,"brep_base64",V::Str).str;if(encoded.empty()||encoded.size()>64u*1024u*1024u)reject("B-rep size limit");const auto& savedReport=require(saved,"report",V::Obj);const auto& savedTopology=require(saved,"topology",V::Obj);require(savedTopology,"references",V::Arr);
    const auto identity=sha256(base64Decode(encoded));if(require(savedTopology,"brep_sha256",V::Str).str!=identity)reject("B-rep digest mismatch");const auto shape=readBrep(encoded);V report=shapeReport(shape,false);const auto& count=require(report,"solids",V::Num);const auto& priorCount=require(savedReport,"solids",V::Num);if(priorCount.num!=count.num||require(savedReport,"faces",V::Num).num!=require(report,"faces",V::Num).num)reject("saved topology census mismatch");
    if(!require(savedReport,"brep_valid",V::Bool).b||require(savedReport,"manufacturing_authoritative",V::Bool).b||require(savedReport,"display_mesh_authoritative",V::Bool).b)reject("saved authority contract mismatch");
    const auto& volume=*report.find("volume_mm3");const auto* oldVolume=savedReport.find("volume_mm3");if(!oldVolume||(count.num==0?oldVolume->t!=V::Null:oldVolume->t!=V::Num||!std::isfinite(oldVolume->num)||!(oldVolume->num>0)||!(volume.num>0)))reject("saved signed-volume contract mismatch");
    const bool multiple=op=="linear_pattern"||op=="circular_pattern"||op=="curve_pattern"||op=="hole_pattern"||op=="split_plane"||op=="split_surface"||op=="split_tool";if(solid&&(count.num<1||count.num>64||(!multiple&&count.num!=1)))reject("solid count violates operation contract");
    if(count.num>1)for(const auto& piece:shapes(shape,TopAbs_SOLID))if(!(properties(piece)>0))reject("saved compound contains a non-positive solid");if(shapes(shape,TopAbs_FACE).empty())reject("wire results require normal derivation replay");
    V diagnostics=V::makeObj();if(const auto* prior=savedReport.find("saved_previous_evaluation")){objectUnique(*prior);if(require(*prior,"provenance",V::Str).str!="saved_previous_evaluation"||require(*prior,"derivation_replayed",V::Bool).b)reject("invalid previous-evaluation provenance");diagnostics=require(*prior,"diagnostics",V::Obj);}
    for(const auto& field:savedReport.obj)if(!genericReportField(field.first))diagnostics.set(field.first,field.second);
    V provenance=V::makeObj();provenance.set("provenance",V::makeStr("saved_previous_evaluation"));provenance.set("derivation_replayed",V::makeBool(false));provenance.set("diagnostics",std::move(diagnostics));report.set("saved_previous_evaluation",std::move(provenance));report.set("derivation_replayed",V::makeBool(false));report.set("snapshot_validation",V::makeStr("current_exact_brep_measurements_and_references"));
    V validatedTopology=topology(savedTopology,shape,identity),result=V::makeObj();result.set("brep_base64",V::makeStr(encoded));result.set("report",std::move(report));result.set("mesh",meshData(shape));result.set("topology",validatedTopology);
    // Same local shape already passed readBrep/BRepCheck, exact measurement,
    // census/sign checks and every saved binding guard. Reuse it privately;
    // arbitrary serialized callers still use the validating public completion.
    result=completeSelectionReferencesOnShape(std::move(result),shape,identity);
    const auto& before=require(validatedTopology,"references",V::Arr),&after=require(require(result,"topology",V::Obj),"references",V::Arr);if(before.arr.size()!=after.arr.size())reject("saved reference inventory is incomplete");for(size_t i=0;i<before.arr.size();++i)for(const char* key:{"name","kind","status","indices"}){const auto* prior=before.arr[i].find(key),*completed=after.arr[i].find(key);if(!prior||!completed||prior->dump()!=completed->dump())reject("saved reference bindings changed during exact completion");}
    // Metadata JSON itself is an input to later native requests. Preserve its
    // validated representation, including absent optional fields and inactive
    // null descriptors, so equivalent saved geometry does not cascade misses.
    result.set("topology",std::move(validatedTopology));return result;
}
}
