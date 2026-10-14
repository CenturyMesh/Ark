// Included by solid_modeling.cpp inside ark::occt, after its exact B-rep,
// topology propagation and finish helpers. Header is included at file scope.
namespace {
TopoDS_Shape holesCheckedCut(const TopoDS_Shape& input,const TopoDS_Shape& cutter,
                            std::vector<Ref>& refs,const std::string& label,double& removed){
    BRepCheck_Analyzer toolCheck(cutter,true);if(!toolCheck.IsValid()||shapes(cutter,TopAbs_SOLID).empty())throw std::runtime_error(label+": invalid analytic cutter");
    BRepAlgoAPI_Cut cut(input,cutter);cut.Build();if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error(label+": exact Boolean cut failed");
    const auto result=cut.Shape();shapeReport(result,true);removed=properties(input)-properties(result);
    if(!std::isfinite(removed)||removed<=1e-8)throw std::runtime_error(label+": cutter removes no positive material (outside, duplicate or inactive location)");
    refs=propagated(refs,result,cut);return result;
}
double holesForwardExtent(const std::array<double,6>& b,const gp_Pnt& p,const gp_Dir& d){
    double end=-1e300;for(int mask=0;mask<8;++mask){gp_Pnt corner(b[(mask&1)?3:0],b[(mask&2)?4:1],b[(mask&4)?5:2]);end=std::max(end,gp_Vec(p,corner).Dot(gp_Vec(d)));}
    if(end<=1e-6)throw std::runtime_error("Through-hole direction has no positive source span");return end+1e-4;
}
void holesEntry(const TopoDS_Shape& source,const gp_Pnt& p,const gp_Dir& d,double depth,const std::string& name){
    BRepClass3d_SolidClassifier boundary(source,p,1e-6);if(boundary.State()!=TopAbs_ON)throw std::runtime_error("Hole "+name+": entry position must lie on the exact source boundary");
    const double probe=std::min(1e-4,depth*.01);BRepClass3d_SolidClassifier inward(source,p.Translated(gp_Vec(d)*probe),1e-6);
    if(inward.State()!=TopAbs_IN)throw std::runtime_error("Hole "+name+": direction must enter the source solid");
}
void holesBlindContainment(const TopoDS_Shape& source,const gp_Pnt& origin,const gp_Dir& direction,
                           double radius,double depth,const std::string& name){
    // Verify the full cylindrical wall/bottom is inside stock beyond the entry
    // plane. A flat-bottom blind hole cannot silently break out at a side/end.
    const double inset=std::min(1e-4,depth*.01);const auto body=BRepPrimAPI_MakeCylinder(gp_Ax2(origin.Translated(gp_Vec(direction)*inset),direction),radius,depth-inset).Shape();
    BRepAlgoAPI_Common common(source,body);common.Build();if(!common.IsDone()||common.HasErrors())throw std::runtime_error("Hole "+name+": blind containment Boolean failed");
    const double wanted=properties(body),found=properties(common.Shape());if(wanted-found>std::max(1e-6,wanted*1e-8))throw std::runtime_error("Hole "+name+": blind cutter breaks out of exact source stock");
}
void holesSemanticRefs(const TopoDS_Shape& shape,const gp_Pnt& origin,const gp_Dir& direction,
                       double radius,const std::string& name,std::vector<Ref>& refs){
    std::vector<TopoDS_Shape> walls;for(const auto& face:shapes(shape,TopAbs_FACE)){
        BRepAdaptor_Surface surface(TopoDS::Face(face));if(surface.GetType()!=GeomAbs_Cylinder)continue;const auto cylinder=surface.Cylinder();
        if(std::abs(cylinder.Radius()-radius)>1e-6||std::abs(std::abs(cylinder.Axis().Direction().Dot(direction))-1)>1e-10)continue;
        const gp_Vec delta(origin,cylinder.Location()),transverse=delta-gp_Vec(direction)*delta.Dot(gp_Vec(direction));if(transverse.Magnitude()>1e-6)continue;walls.push_back(face);
    }
    if(walls.size()==1){add(refs,name+".wall","face",walls.front());add(refs,name+".axis","axis",walls.front());}
    else if(!walls.empty()){Ref ref{name+".wall","face","split",{}, {}, {}};refs.push_back(ref);ref.name=name+".axis";ref.kind="axis";refs.push_back(ref);}
}
void holesRequireNominalCylinder(const TopoDS_Shape& shape,const gp_Pnt& origin,const gp_Dir& axis,
                                double radius,const std::string& label){
    bool found=false;for(const auto& face:shapes(shape,TopAbs_FACE)){BRepAdaptor_Surface surface(TopoDS::Face(face));if(surface.GetType()!=GeomAbs_Cylinder)continue;const auto cylinder=surface.Cylinder();
        if(std::abs(cylinder.Radius()-radius)>1e-6||std::abs(std::abs(cylinder.Axis().Direction().Dot(axis))-1)>1e-10)continue;
        const gp_Vec delta(origin,cylinder.Location()),transverse=delta-gp_Vec(axis)*delta.Dot(gp_Vec(axis));if(transverse.Magnitude()<=1e-6)found=true;}
    if(!found)throw std::runtime_error(label+": exact source needs a coaxial nominal cylindrical shaft/bore; images, meshes and approximate recognition are not authority");
}
}

V holesOrchestratorNative(const V& request){
    const V plan=ark::resolveHolesRecipe(request);const V& encoded=need(request,"input_brep");if(encoded.t!=V::Str)throw std::runtime_error("Holes Orchestrator input must be an exact native B-rep");
    const TopoDS_Shape source=readBrep(encoded.str);shapeReport(source,true);TopoDS_Shape current=source;auto refs=inputRefs(request,"input_brep","input_topology",source);
    const auto sourceBounds=bounds(source);const double sourceVolume=properties(source);V receipt=need(plan,"report"),locations=V::makeArr();const auto kind=word(plan,"kind"),termination=word(plan,"termination"),recess=word(plan,"recess");
    const bool shaft=kind=="groove_external",bore=kind=="groove_internal",groove=shaft||bore;
    for(const auto& location:need(plan,"points").arr){const auto name=word(location,"name");const gp_Pnt origin=point(need(location,"position"),"hole position"),directionPoint=point(need(location,"direction"),"hole direction");const gp_Dir axis(directionPoint.X(),directionPoint.Y(),directionPoint.Z());const std::string prefix="holes."+name;
        double removed=0;V evidence=V::makeObj();evidence.set("name",V::makeStr(name));evidence.set("position_mm",xyz(origin));evidence.set("direction",direction(axis));
        if(groove){const double nominal=positive(plan,"nominal_diameter"),diameter=positive(plan,"groove_diameter"),width=positive(plan,"groove_width");holesRequireNominalCylinder(current,origin,axis,nominal/2,prefix);
            TopoDS_Shape cutter;if(shaft){const auto outer=BRepPrimAPI_MakeCylinder(gp_Ax2(origin,axis),nominal/2+1e-4,width).Shape(),inner=BRepPrimAPI_MakeCylinder(gp_Ax2(origin.Translated(gp_Vec(axis)*-1e-4),axis),diameter/2,width+2e-4).Shape();BRepAlgoAPI_Cut ring(outer,inner);ring.Build();if(!ring.IsDone()||ring.HasErrors())throw std::runtime_error(prefix+": analytic annular groove cutter failed");cutter=ring.Shape();}
            else cutter=BRepPrimAPI_MakeCylinder(gp_Ax2(origin,axis),diameter/2,width).Shape();
            current=holesCheckedCut(current,cutter,refs,prefix,removed);const double expected=3.14159265358979323846/4*std::abs(nominal*nominal-diameter*diameter)*width;
            if(std::abs(removed-expected)>std::max(1e-5,expected*1e-7))throw std::runtime_error(prefix+": groove does not remove the complete declared circular band from nominal cylindrical stock");
            evidence.set("axial_width_mm",V::makeNum(width));evidence.set("complete_circular_band_checked",V::makeBool(true));holesSemanticRefs(current,origin,axis,diameter/2,prefix+".groove",refs);
        }else{
            const double diameter=positive(plan,"diameter"),radius=diameter/2,depth=termination=="blind"?positive(plan,"depth"):holesForwardExtent(sourceBounds,origin,axis);holesEntry(current,origin,axis,depth,name);
            if(termination=="blind")holesBlindContainment(current,origin,axis,radius,depth,name);
            // This fixed axial overtravel extends the tool outside stock. It
            // changes no Boolean tolerance and avoids a coincident entry cap.
            const double lead=1e-4;const auto cutter=BRepPrimAPI_MakeCylinder(gp_Ax2(origin.Translated(gp_Vec(axis)*-lead),axis),radius,depth+lead).Shape();
            current=holesCheckedCut(current,cutter,refs,prefix,removed);double totalRemoved=removed;
            if(recess!="none"){const double recessRadius=positive(plan,"recess_diameter")/2,recessDepth=positive(plan,"recess_depth");TopoDS_Shape counter;
                if(recess=="counterbore")counter=BRepPrimAPI_MakeCylinder(gp_Ax2(origin.Translated(gp_Vec(axis)*-lead),axis),recessRadius,recessDepth+lead).Shape();
                else{const double slope=(recessRadius-radius)/recessDepth;counter=BRepPrimAPI_MakeCone(gp_Ax2(origin.Translated(gp_Vec(axis)*-lead),axis),recessRadius+slope*lead,radius,recessDepth+lead).Shape();}
                // Validate recess containment against the original stock, since
                // the central bore is deliberately empty in the current result.
                holesBlindContainment(source,origin,axis,recessRadius,recessDepth,name+" recess");
                current=holesCheckedCut(current,counter,refs,prefix+"."+recess,removed);totalRemoved+=removed;
            }
            removed=totalRemoved;evidence.set("cut_extent_mm",V::makeNum(depth));evidence.set("entry_boundary_checked",V::makeBool(true));evidence.set("blind_stock_containment_checked",V::makeBool(termination=="blind"));holesSemanticRefs(current,origin,axis,radius,prefix,refs);
        }
        evidence.set("removed_volume_mm3",V::makeNum(removed));locations.arr.push_back(evidence);
    }
    V result=finish(current,refs),report=need(result,"report");receipt.set("locations",locations);receipt.set("source_volume_mm3",V::makeNum(sourceVolume));receipt.set("removed_volume_mm3",V::makeNum(sourceVolume-properties(current)));receipt.set("exact_geometry",V::makeBool(true));receipt.set("source_linked",V::makeBool(true));receipt.set("body_discarded",V::makeBool(false));receipt.set("boolean_tolerance_changed",V::makeBool(false));receipt.set("sheet_flat_lineage",V::makeBool(false));receipt.set("scope",V::makeStr("one native solid; source-linked analytic hole/groove cuts; no manufacturing qualification, tapped-flank B-rep or sheet flat-pattern lineage"));report.set("holes_orchestrator",receipt);result.set("report",report);return result;
}
