// Exact, editable rubber stock. No constitutive/strength claims are made by this producer.
TopoDS_Wire rubberRaceWire059(double distance,double radius,double z){
    const double h=distance*.5;
    const gp_Pnt a(-h,radius,z),b(h,radius,z),c(h,-radius,z),d(-h,-radius,z);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(a,b));
    wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(b,gp_Pnt(h+radius,0,z),c).Value()));
    wire.Add(BRepBuilderAPI_MakeEdge(c,d));
    wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(d,gp_Pnt(-h-radius,0,z),a).Value()));
    if(!wire.IsDone())throw std::runtime_error("Rubber racetrack wire failed");
    return wire.Wire();
}
TopoDS_Shape rubberEllipseRing059(double major,double radial,double axial){
    // In the XZ meridian plane. The ellipse must remain strictly outside the revolution axis.
    const gp_Ax2 axes(gp_Pnt(major,0,0),gp_Dir(0,-1,0),radial>=axial?gp_Dir(1,0,0):gp_Dir(0,0,1));
    Handle(Geom_Ellipse) ellipse=new Geom_Ellipse(axes,std::max(radial,axial),std::min(radial,axial));
    BRepBuilderAPI_MakeWire wire(BRepBuilderAPI_MakeEdge(ellipse).Edge());
    BRepBuilderAPI_MakeFace face(wire.Wire());
    BRepPrimAPI_MakeRevol revolve(face.Face(),gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)));
    if(!revolve.IsDone())throw std::runtime_error("Rubber tire revolution failed");
    return revolve.Shape();
}
V rubberBodyNative059(const V& request){
    const auto kind=word(request,"kind");
    std::set<std::string> allowed={"op","kind","relief"};
    std::vector<const char*> dimensions;
    if(kind=="belt"||kind=="track")dimensions={"center_distance_mm","radius_mm","thickness_mm","width_mm"};
    else if(kind=="tire")dimensions={"major_radius_mm","radial_radius_mm","axial_radius_mm","wall_mm"};
    else if(kind=="ball")dimensions={"radius_mm","wall_mm"};
    else throw std::runtime_error("Rubber kind must be belt, track, tire or ball");
    for(auto key:dimensions)allowed.insert(key);
    for(const auto& entry:request.obj)if(!allowed.count(entry.first))throw std::runtime_error("Unknown rubber body field: "+entry.first);
    TopoDS_Shape body;V surface=V::makeObj();
    if(kind=="belt"||kind=="track"){
        const double d=positive(request,"center_distance_mm"),r=positive(request,"radius_mm"),t=positive(request,"thickness_mm"),w=positive(request,"width_mm");
        if(t>=2*r)throw std::runtime_error("Belt thickness must be smaller than twice its pitch radius");
        auto stock=[&](double radius){BRepBuilderAPI_MakeFace f(rubberRaceWire059(d,radius,-w*.5));BRepPrimAPI_MakePrism p(f.Face(),gp_Vec(0,0,w));if(!p.IsDone())throw std::runtime_error("Belt extrusion failed");return p.Shape();};
        BRepAlgoAPI_Cut cut(stock(r+t*.5),stock(r-t*.5));if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Belt annulus failed");body=cut.Shape();
        surface.set("kind",V::makeStr("racetrack"));surface.set("center_distance_mm",V::makeNum(d));surface.set("radius_mm",V::makeNum(r+t*.5));surface.set("width_mm",V::makeNum(w));
    }else if(kind=="tire"){
        const double major=positive(request,"major_radius_mm"),radial=positive(request,"radial_radius_mm"),axial=positive(request,"axial_radius_mm"),wall=scalar(request,"wall_mm");
        if(major<=radial||wall<0||wall>=std::min(radial,axial))throw std::runtime_error("Tire requires major > radial radius; wall is 0 (solid) or less than both section radii");
        body=rubberEllipseRing059(major,radial,axial);
        if(wall>0){BRepAlgoAPI_Cut cut(body,rubberEllipseRing059(major,radial-wall,axial-wall));if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Tire cavity failed");body=cut.Shape();}
        surface.set("kind",V::makeStr("torus"));surface.set("major_radius_mm",V::makeNum(major));surface.set("radial_radius_mm",V::makeNum(radial));surface.set("axial_radius_mm",V::makeNum(axial));surface.set("v_start_deg",V::makeNum(-60));surface.set("v_end_deg",V::makeNum(60));
    }else{
        const double r=positive(request,"radius_mm"),wall=scalar(request,"wall_mm");if(wall<0||wall>=r)throw std::runtime_error("Ball wall is 0 (solid) or smaller than radius");
        body=BRepPrimAPI_MakeSphere(r).Shape();
        if(wall>0){BRepAlgoAPI_Cut cut(body,BRepPrimAPI_MakeSphere(r-wall).Shape());if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Ball cavity failed");body=cut.Shape();}
        surface.set("kind",V::makeStr("sphere"));surface.set("radius_mm",V::makeNum(r));
    }
    requireOnePositiveSolid(body);
    const double baseVolume=properties(body);unsigned motifs=0;
    if(const auto* relief=request.find("relief")){
        if(relief->t!=V::Obj)throw std::runtime_error("Rubber relief must be an object");
        const std::set<std::string> keys={"pattern","motif","height_mm","width_mm","length_mm","embed_mm"};for(const auto& item:relief->obj)if(!keys.count(item.first))throw std::runtime_error("Unknown rubber relief field: "+item.first);
        V recipe=need(*relief,"pattern");if(recipe.t!=V::Obj||recipe.find("surface")||recipe.find("placement")||recipe.find("normal_offset_mm"))throw std::runtime_error("Relief surface and placement come from the authored rubber body");recipe.set("surface",surface);
        ark::RubberPatternPlan plan(recipe);if(plan.count()>256)throw std::runtime_error("Exact relief supports at most 256 motifs; use procedural appearance and paged pattern preview for dense detail");
        const auto motif=word(*relief,"motif");const double h=positive(*relief,"height_mm"),w=positive(*relief,"width_mm"),embed=positive(*relief,"embed_mm");
        if(embed>=h)throw std::runtime_error("Relief embed must be smaller than height");
        TopoDS_Shape seed;
        if(motif=="spike")seed=BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0,0,-embed),gp_Dir(0,0,1)),w*.5,0,h+embed).Shape();
        else if(motif=="lug"){const double length=positive(*relief,"length_mm");seed=BRepPrimAPI_MakeBox(gp_Pnt(-length*.5,-w*.5,-embed),length,w,h+embed).Shape();}
        else if(motif=="pebble"){if(std::abs(h-w*.5)>1e-9)throw std::runtime_error("Spherical pebble height must equal width/2");seed=BRepPrimAPI_MakeSphere(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1)),h,std::asin(-embed/h),K_PI*.5).Shape();}
        else throw std::runtime_error("Relief motif must be spike, lug or pebble");
        for(uint32_t i=0;i<plan.count();++i){
            const auto f=plan.frame(i);gp_Trsf tr;
            tr.SetValues(f.tangent.x,f.bitangent.x,f.normal.x,f.position_mm.x,f.tangent.y,f.bitangent.y,f.normal.y,f.position_mm.y,f.tangent.z,f.bitangent.z,f.normal.z,f.position_mm.z);
            BRepBuilderAPI_Transform placed(seed,tr,true);BRepAlgoAPI_Fuse fuse(body,placed.Shape());if(!fuse.IsDone()||fuse.HasErrors())throw std::runtime_error("Rubber relief fuse failed at motif "+std::to_string(i));body=fuse.Shape();requireOnePositiveSolid(body);
        }
        motifs=plan.count();
    }
    std::vector<Ref> refs;unsigned i=0;for(const auto& face:shapes(body,TopAbs_FACE))add(refs,"rubber.face."+std::to_string(++i),"face",face);
    V result=finish(body,refs),receipt=V::makeObj();receipt.set("kind",V::makeStr(kind));receipt.set("base_volume_mm3",V::makeNum(baseVolume));receipt.set("relief_motifs",V::makeNum(motifs));receipt.set("surface",surface);receipt.set("manufacturing_qualified",V::makeBool(false));result.set("rubber",receipt);return result;
}
