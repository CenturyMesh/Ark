// B.03, 031B: kernel trihedra and continuous, piecewise linear straight-spine laws.
TopoDS_Face prepareSweepPath031B(const V& request, Profile& path) {
    const V* orientation=request.find("orientation");if(!orientation)return {};
    if(orientation->t!=V::Obj)throw std::runtime_error("Sweep orientation must be an object");
    const auto& mode=required(*orientation,"mode");
    if(mode.t!=V::Str)throw std::runtime_error("Sweep orientation mode must be a string");
    if(mode.str!="support")return {};
    const auto support=singleSurface031B(required(*orientation,"support_brep"),"Sweep support");
    BRepBuilderAPI_MakeWire wire;
    for(auto& edge:path.edges){edge.edge=edgeOnSupport031B(edge.edge,support,33);wire.Add(edge.edge);if(!wire.IsDone())throw std::runtime_error("Sweep support path reconstruction failed");edge.edge=wire.Edge();}
    path.wire=wire.Wire();
    return support;
}

std::string setSweepOrientation031B(const V& request,const Profile& path,BRepOffsetAPI_MakePipeShell& pipe,const TopoDS_Face& preparedSupport,std::string& solver) {
    const V* orientation=request.find("orientation");if(!orientation)return "corrected_frenet";
    if(orientation->t!=V::Obj)throw std::runtime_error("Sweep orientation must be an object");
    const V& mode=required(*orientation,"mode");
    if(mode.t!=V::Str)throw std::runtime_error("Sweep orientation mode must be text");
    if(request.find("sections"))throw std::runtime_error("Explicit sweep orientation cannot combine with legacy authored station frames");
    for(const auto& field:orientation->obj){
        const bool allowed=field.first=="mode"||(mode.str=="fixed_binormal"&&field.first=="binormal")||(mode.str=="support"&&field.first=="support_brep")||(mode.str=="auxiliary"&&(field.first=="guide"||field.first=="equivalence"));
        if(!allowed)throw std::runtime_error("Unsupported field for sweep orientation "+mode.str+": "+field.first);
    }
    if(mode.str=="corrected_frenet")pipe.SetMode(false);
    else if(mode.str=="frenet")pipe.SetMode(true);
    else if(mode.str=="fixed_binormal") {
        const gp_Vec binormal=vector031B(required(*orientation,"binormal"),"binormal");
        if(binormal.Magnitude()<=1e-12)throw std::runtime_error("Sweep binormal must be nonzero");
        for(const auto& edge:path.edges){BRepAdaptor_Curve curve(edge.edge);for(int i=0;i<=32;++i){gp_Pnt p;gp_Vec tangent;curve.D1(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/32.,p,tangent);if(tangent.Magnitude()<=1e-12||tangent.Normalized().Crossed(binormal.Normalized()).Magnitude()<=1e-8)throw std::runtime_error("Fixed sweep binormal is parallel to a path tangent at a native checkpoint");}}
        pipe.SetMode(gp_Dir(binormal));
    }else if(mode.str=="support") {
        if(preparedSupport.IsNull()||!pipe.SetMode(preparedSupport))throw std::runtime_error("Sweep spine has no usable parameterization on its support face");
    }else if(mode.str=="auxiliary") {
        const auto guide=profileOf(required(*orientation,"guide"),false);
        const auto& equivalence=required(*orientation,"equivalence");
        if(equivalence.t!=V::Str||(equivalence.str!="curvilinear"&&equivalence.str!="normal_plane"))throw std::runtime_error("Auxiliary sweep equivalence must be curvilinear or normal_plane");
        if(path.edges.size()==1&&guide.edges.size()==1&&BRepAdaptor_Curve(path.edges[0].edge).GetType()==GeomAbs_Line&&BRepAdaptor_Curve(guide.edges[0].edge).GetType()==GeomAbs_Line&&gp_Vec(path.edges[0].first,path.edges[0].last).IsParallel(gp_Vec(guide.edges[0].first,guide.edges[0].last),1e-12)){
            // Parallel analytic spines define a constant normal exactly. Avoid
            // approximating this constant frame through a guide interpolation.
            const gp_Vec tangent=gp_Vec(path.edges[0].first,path.edges[0].last).Normalized();gp_Vec normal(path.edges[0].first,guide.edges[0].first);normal-=tangent*normal.Dot(tangent);
            if(normal.Magnitude()<=kTolerance)throw std::runtime_error("Auxiliary straight spine has no transverse guide separation");
            if(equivalence.str=="normal_plane"){const double a=gp_Vec(path.edges[0].first,guide.edges[0].first).Dot(tangent),b=gp_Vec(path.edges[0].first,guide.edges[0].last).Dot(tangent),length=path.edges[0].first.Distance(path.edges[0].last);if(std::min(a,b)>kTolerance||std::max(a,b)<length-kTolerance)throw std::runtime_error("Auxiliary guide does not meet every normal plane of the finite spine");}
            pipe.SetMode(gp_Ax2(path.edges[0].first,gp_Dir(tangent),gp_Dir(normal)));solver="analytic_constant_frame_of_parallel_native_spines";
        }else pipe.SetMode(guide.wire,equivalence.str=="curvilinear",BRepFill_NoContact);
    }else throw std::runtime_error("Unsupported sweep orientation mode: "+mode.str);
    // Existing sweeps retain their kernel defaults. Explicit modes bind fixed tolerances.
    pipe.SetTolerance(kTolerance,kTolerance,1e-6);
    return mode.str;
}

class LinearTwistTrihedron031B final:public GeomFill_TrihedronLaw {
public:
    LinearTwistTrihedron031B(gp_Vec tangent,gp_Vec x,gp_Vec y,double first,double last,double a,double b)
        :tangent_(tangent),x_(x),y_(y),first_(first),last_(last),a_(a),rate_((b-a)/(last-first)){}
    Handle(GeomFill_TrihedronLaw) Copy()const override {
        auto copy=new LinearTwistTrihedron031B(tangent_,x_,y_,first_,last_,a_,a_+rate_*(last_-first_));
        if(!myCurve.IsNull())copy->SetCurve(myCurve);return copy;
    }
    bool D0(double t,gp_Vec& tangent,gp_Vec& normal,gp_Vec& binormal)override {
        const double angle=a_+rate_*(t-first_);tangent=tangent_;normal=x_*std::cos(angle)+y_*std::sin(angle);binormal=-x_*std::sin(angle)+y_*std::cos(angle);return true;
    }
    bool D1(double t,gp_Vec& tangent,gp_Vec& dt,gp_Vec& normal,gp_Vec& dn,gp_Vec& binormal,gp_Vec& db)override {
        D0(t,tangent,normal,binormal);dt=gp_Vec(0,0,0);dn=binormal*rate_;db=-normal*rate_;return true;
    }
    bool D2(double t,gp_Vec& tangent,gp_Vec& dt,gp_Vec& ddt,gp_Vec& normal,gp_Vec& dn,gp_Vec& ddn,gp_Vec& binormal,gp_Vec& db,gp_Vec& ddb)override {
        D1(t,tangent,dt,normal,dn,binormal,db);ddt=gp_Vec(0,0,0);ddn=-normal*(rate_*rate_);ddb=-binormal*(rate_*rate_);return true;
    }
    int NbIntervals(GeomAbs_Shape)const override{return 1;}
    void Intervals(NCollection_Array1<double>& values,GeomAbs_Shape)const override{values(values.Lower())=first_;values(values.Lower()+1)=last_;}
    void GetAverageLaw(gp_Vec& tangent,gp_Vec& normal,gp_Vec& binormal)override {
        tangent=tangent_;
        if(std::abs(rate_)<1e-14){D0(first_,tangent,normal,binormal);return;}
        const double b=a_+rate_*(last_-first_),span=b-a_;
        const double c=(std::sin(b)-std::sin(a_))/span,s=(std::cos(a_)-std::cos(b))/span;
        normal=x_*c+y_*s;binormal=-x_*s+y_*c;
    }
    bool IsConstant()const override{return rate_==0;}
private:
    gp_Vec tangent_,x_,y_;double first_,last_,a_,rate_;
};

TopoDS_Wire splitClosedSectionEdges031B(const TopoDS_Wire& source) {
    BRepBuilderAPI_MakeWire wire;
    for(BRepTools_WireExplorer it(source);it.More();it.Next()){
        const auto edge=it.Current();double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);
        if(!curve.IsNull()&&curve->Value(first).Distance(curve->Value(last))<=kTolerance){
            BRepBuilderAPI_MakeEdge a(curve,first,(first+last)/2),b(curve,(first+last)/2,last);
            if(!a.IsDone()||!b.IsDone())throw std::runtime_error("Closed section edge subdivision failed");
            auto ea=a.Edge(),eb=b.Edge();if(edge.Orientation()==TopAbs_REVERSED){ea.Reverse();eb.Reverse();wire.Add(eb);wire.Add(ea);}else{wire.Add(ea);wire.Add(eb);}
        }else wire.Add(edge);
        if(!wire.IsDone())throw std::runtime_error("Law sweep section wire reconstruction failed");
    }
    return wire.Wire();
}

V lawSweep031B(const V& request) {
    if(request.find("sections"))throw std::runtime_error("Sweep laws and varying sections are exclusive");
    if(flag(request,"contact",false))throw std::runtime_error("Continuous twist/scale laws use the authored section-frame origin; contact translation is unsupported");
    if(!flag(request,"correction",true))throw std::runtime_error("Continuous twist/scale laws require correction:true to place the authored section frame normal on the spine");
    const bool solid=flag(request,"solid",false);
    const auto path=profileOf(required(request,"path"),false),section=profileOf(required(request,"section"),solid);
    if(path.closed||path.edges.size()!=1||BRepAdaptor_Curve(path.edges.front().edge).GetType()!=GeomAbs_Line)
        throw std::runtime_error("Continuous twist/scale laws currently require one open straight native spine edge");
    const auto& laws=required(request,"laws");
    if(laws.t!=V::Obj)throw std::runtime_error("Sweep laws must be an object");
    for(const auto& field:laws.obj)if(field.first!="stations"&&field.first!="interpolation")throw std::runtime_error("Unknown sweep law field: "+field.first);
    const auto& interpolation=required(laws,"interpolation");
    if(interpolation.t!=V::Str||interpolation.str!="linear")throw std::runtime_error("Sweep laws support explicit linear interpolation only");
    const auto& stations=required(laws,"stations");
    if(stations.t!=V::Arr||stations.arr.size()<2||stations.arr.size()>32)throw std::runtime_error("Sweep laws require 2..32 stations");
    struct Station {double fraction,angle,scale;};std::vector<Station> values;
    for(const auto& station:stations.arr){
        if(station.t!=V::Obj)throw std::runtime_error("Sweep law station must be an object");
        for(const auto& field:station.obj)if(field.first!="fraction"&&field.first!="twist_deg"&&field.first!="scale")throw std::runtime_error("Unknown sweep law station field: "+field.first);
        Station value{number(required(station,"fraction"),"fraction"),number(required(station,"twist_deg"),"twist_deg")*std::acos(-1.)/180.,number(required(station,"scale"),"scale")};
        if(value.fraction<0||value.fraction>1||(!values.empty()&&value.fraction<=values.back().fraction))throw std::runtime_error("Sweep law fractions must increase strictly in 0..1");
        if(value.scale<=1e-6||value.scale>1e3)throw std::runtime_error("Sweep scale must be in (1e-6,1000]");
        if(std::abs(value.angle)>100*std::acos(-1.))throw std::runtime_error("Sweep twist is limited to 50 authored revolutions");
        if(!values.empty()&&std::abs(value.angle-values.back().angle)>2*std::acos(-1.))throw std::runtime_error("Each law interval is limited to one full twist; add explicit stations");
        values.push_back(value);
    }
    if(values.front().fraction!=0||values.back().fraction!=1)throw std::runtime_error("Sweep laws must explicitly include fractions 0 and 1");
    const gp_Pnt start=path.edges.front().first,end=path.edges.front().last;
    gp_Vec tangent(start,end);const double length=tangent.Magnitude();if(length<=kTolerance)throw std::runtime_error("Sweep law spine is degenerate");tangent.Normalize();
    const auto& frame=section.frame;gp_Vec x(frame[0],frame[1],frame[2]);std::string orientationMode="corrected_frenet";
    if(const V* orientation=request.find("orientation")) {
        if(orientation->t!=V::Obj)throw std::runtime_error("Sweep orientation must be an object");
        const auto& mode=required(*orientation,"mode");
        if(mode.t!=V::Str||(mode.str!="corrected_frenet"&&mode.str!="fixed_binormal"))throw std::runtime_error("Straight-spine twist laws support corrected_frenet or fixed_binormal orientation");
        orientationMode=mode.str;
        for(const auto& field:orientation->obj)if(field.first!="mode"&&!(mode.str=="fixed_binormal"&&field.first=="binormal"))throw std::runtime_error("Unknown law sweep orientation field: "+field.first);
        if(mode.str=="fixed_binormal") {gp_Vec b=vector031B(required(*orientation,"binormal"),"binormal");if(b.Magnitude()<=1e-12)throw std::runtime_error("Sweep binormal must be nonzero");b=b-tangent*b.Dot(tangent);if(b.Magnitude()<=1e-8)throw std::runtime_error("Sweep binormal is parallel to its spine");b.Normalize();x=b.Crossed(tangent);}
    }
    x=x-tangent*x.Dot(tangent);if(x.Magnitude()<=1e-10)throw std::runtime_error("Law sweep section X axis is parallel to its spine");x.Normalize();const gp_Vec y=tangent.Crossed(x);
    // Authored section coordinates are retained in their frame, then positioned
    // by the continuous location/scale laws. A nonplanar section is rejected.
    gp_Trsf source;source.SetValues(frame[0],frame[4],frame[8],frame[12],frame[1],frame[5],frame[9],frame[13],frame[2],frame[6],frame[10],frame[14]);
    // Matching finite half-edges on both the swept sides and the exact end caps
    // avoids asking sewing to match one closed analytic cap edge to two fitted
    // side edges. This changes topology only, preserving the exact source curve.
    const auto local=splitClosedSectionEdges031B(TopoDS::Wire(transformed(section.wire,source.Inverted())));
    for(const auto& edge:shapes(local,TopAbs_EDGE)){BRepAdaptor_Curve curve(TopoDS::Edge(edge));for(int i=0;i<=32;++i)if(std::abs(curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/32.).Z())>kTolerance)throw std::runtime_error("Law sweep requires a planar section in its authored frame");}
    const Handle(Geom_Curve) spine=new Geom_Line(start,gp_Dir(tangent));
    BRepBuilderAPI_Sewing sewing(kTolerance);sewing.SetMaxTolerance(kTolerance);sewing.SetLocalTolerancesMode(false);
    double maxFit=0;size_t generated=0;std::array<std::vector<TopoDS_Edge>,2> endRims;
    for(size_t segment=1;segment<values.size();++segment){
        const auto& a=values[segment-1];const auto& b=values[segment];const double first=a.fraction*length,last=b.fraction*length;
        Handle(GeomFill_TrihedronLaw) trihedron=new LinearTwistTrihedron031B(tangent,x,y,first,last,a.angle,b.angle);
        Handle(GeomFill_CurveAndTrihedron) location=new GeomFill_CurveAndTrihedron(trihedron);
        location->SetCurve(new GeomAdaptor_Curve(spine,first,last));
        Handle(Law_Linear) scale=new Law_Linear();scale->Set(first,a.scale,last,b.scale);
        for(const auto& shape:shapes(local,TopAbs_EDGE)) {
            const auto edge=TopoDS::Edge(shape);double u0,u1;Handle(Geom_Curve) curve=BRep_Tool::Curve(edge,u0,u1);
            if(curve.IsNull())throw std::runtime_error("Law sweep section edge has no native curve");
            const int pieces=curve->Value(u0).Distance(curve->Value(u1))<=kTolerance?2:1;
            for(int piece=0;piece<pieces;++piece){
            Handle(Geom_Curve) boundedCurve=new Geom_TrimmedCurve(curve,u0+(u1-u0)*piece/pieces,u0+(u1-u0)*(piece+1)/pieces);
            Handle(GeomFill_EvolvedSection) sectionLaw=new GeomFill_EvolvedSection(boundedCurve,scale);
            GeomFill_Sweep sweep(location,false);sweep.SetDomain(first,last,first,last);sweep.SetTolerance(1e-8,1e-8,1e-9,1e-7);
            sweep.Build(sectionLaw,GeomFill_Section,GeomAbs_C2,12,128);
            if(!sweep.IsDone()||!std::isfinite(sweep.ErrorOnSurface())||sweep.ErrorOnSurface()>kTolerance)throw std::runtime_error("Continuous law sweep fit exceeds fixed 1e-6 mm tolerance");
            maxFit=std::max(maxFit,sweep.ErrorOnSurface());
            Handle(Geom_Surface) sweptSurface=sweep.Surface();
            if(auto spline=Handle(Geom_BSplineSurface)::DownCast(sweptSurface);!spline.IsNull()&&spline->IsUClosed()&&!spline->IsUPeriodic())spline->SetUPeriodic();
            BRepBuilderAPI_MakeFace face(sweptSurface,kTolerance);if(!face.IsDone())throw std::runtime_error("Continuous law sweep face construction failed");
            auto faceShape=face.Face();if(edge.Orientation()==TopAbs_REVERSED)faceShape.Reverse();sewing.Add(faceShape);++generated;
            if(solid)for(int endIndex=0;endIndex<2;++endIndex){
                if((endIndex==0&&segment!=1)||(endIndex==1&&segment+1!=values.size()))continue;
                int matches=0;const double planeDistance=endIndex==0?0:length;
                for(const auto& boundary:shapes(faceShape,TopAbs_EDGE)){
                    auto rim=TopoDS::Edge(boundary);BRepAdaptor_Curve boundaryCurve(rim);bool onPlane=true;
                    for(int checkpoint=0;checkpoint<=2;++checkpoint){const auto p=boundaryCurve.Value(boundaryCurve.FirstParameter()+(boundaryCurve.LastParameter()-boundaryCurve.FirstParameter())*checkpoint/2.);onPlane&=std::abs(gp_Vec(start,p).Dot(tangent)-planeDistance)<=1e-7;}
                    if(onPlane){rim.Orientation(edge.Orientation());endRims[endIndex].push_back(rim);++matches;}
                }
                if(matches!=1)throw std::runtime_error("Law sweep cannot identify its native end-profile edge uniquely");
            }
            }
        }
    }
    auto positioned=[&](const Station& station) {
        const gp_Pnt origin=start.Translated(tangent*(station.fraction*length));
        const gp_Vec sx=x*std::cos(station.angle)+y*std::sin(station.angle),sy=tangent.Crossed(sx);
        gp_Trsf pose;pose.SetValues(sx.X(),sy.X(),tangent.X(),origin.X(),sx.Y(),sy.Y(),tangent.Y(),origin.Y(),sx.Z(),sy.Z(),tangent.Z(),origin.Z());
        gp_Trsf scaling;scaling.SetScale(gp_Pnt(0,0,0),station.scale);
        return TopoDS::Wire(transformed(local,pose*scaling));
    };
    double endProfileError=0;int endProfileSamples=0;
    if(solid)for(int endIndex=0;endIndex<2;++endIndex){
        BRepBuilderAPI_MakeWire rim;for(const auto& edge:endRims[endIndex])rim.Add(edge);
        if(!rim.IsDone()||!BRep_Tool::IsClosed(rim.Wire()))throw std::runtime_error("Law sweep fitted end profile is not closed");
        const auto original=positioned(endIndex==0?values.front():values.back());
        // Check both directions against the authored exact profile before capping
        // the actual fitted rim. Shared edge custody gives a watertight shell.
        for(int direction=0;direction<2;++direction){const auto& from=direction==0?rim.Wire():original;const auto& to=direction==0?original:rim.Wire();for(const auto& edge:shapes(from,TopAbs_EDGE)){BRepAdaptor_Curve curve(TopoDS::Edge(edge));for(int j=0;j<=16;++j){const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*j/16.);BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(),to);distance.Perform();if(!distance.IsDone())throw std::runtime_error("Law sweep end-profile measurement failed");endProfileError=std::max(endProfileError,distance.Value());++endProfileSamples;}}}
        if(endProfileError>kTolerance)throw std::runtime_error("Law sweep fitted end profile exceeds fixed 1e-6 mm acceptance");
        BRepBuilderAPI_MakeFace cap(rim.Wire(),true);if(!cap.IsDone())throw std::runtime_error("Law sweep cap construction failed");auto face=cap.Face();if(endIndex==0)face.Reverse();sewing.Add(face);++generated;
    }
    sewing.Perform();TopoDS_Shape resultShape=sewing.SewedShape();
    if(resultShape.IsNull()||sewing.NbDeletedFaces()!=0||sewing.NbDegeneratedShapes()!=0||sewing.NbMultipleEdges()!=0||shapes(resultShape,TopAbs_FACE).size()!=generated)throw std::runtime_error("Law sweep refuses missing, degenerate or non-manifold faces");
    if(solid){const auto shells=shapes(resultShape,TopAbs_SHELL);if(shells.size()!=1||sewing.NbFreeEdges()!=0||!BRep_Tool::IsClosed(shells.front())){
        const auto firstProfile=positioned(values.front()),lastProfile=positioned(values.back());double rimError=0;
        for(int i=1;i<=sewing.NbFreeEdges();++i){BRepAdaptor_Curve rim(sewing.FreeEdge(i));for(int j=0;j<=16;++j){const auto p=rim.Value(rim.FirstParameter()+(rim.LastParameter()-rim.FirstParameter())*j/16.);const auto vertex=BRepBuilderAPI_MakeVertex(p).Vertex();BRepExtrema_DistShapeShape da(vertex,firstProfile),db(vertex,lastProfile);da.Perform();db.Perform();if(da.IsDone()&&db.IsDone())rimError=std::max(rimError,std::min(da.Value(),db.Value()));}}
        throw std::runtime_error("Continuous law sweep did not produce one closed shell: shells="+std::to_string(shells.size())+", free_edges="+std::to_string(sewing.NbFreeEdges())+", free_edge_to_end_profile_mm="+std::to_string(rimError));
    }BRepBuilderAPI_MakeSolid makeSolid(TopoDS::Shell(shells.front()));if(!makeSolid.IsDone())throw std::runtime_error("Continuous law sweep solid construction failed");resultShape=makeSolid.Solid();if(properties(resultShape)<0)resultShape.Reverse();}
    V result=tag(shapeResult(resultShape,solid),"sweep"),report=required(result,"report");
    report.set("orientation_mode",V::makeStr(orientationMode));
    report.set("law_stations",stations);report.set("law_interpolation",V::makeStr("piecewise_linear_twist_radians_and_uniform_scale_by_exact_straight_spine_arclength"));
    report.set("law_geometry",V::makeStr("native_sweep_of_continuous_analytic_frame_and_scale_laws"));
    report.set("kernel_max_fit_error_mm",V::makeNum(maxFit));report.set("fit_tolerance_mm",V::makeNum(kTolerance));
    report.set("end_profile_max_sampled_error_mm",V::makeNum(endProfileError));report.set("end_profile_samples_checked",V::makeNum(endProfileSamples));
    report.set("law_scope",V::makeStr("One straight spine; exact angle/scale law with analytic frame derivatives on each interval, approximated by the native B-spline sweep; kernel fit error is an algorithm estimate, not an independently certified continuous error bound; derivative jumps at law stations remain authored"));
    report.set("continuous_error_bound_certified",V::makeBool(false));report.set("self_intersections_checked",V::makeBool(false));report.set("display_mesh_authoritative",V::makeBool(false));
    result.set("report",report);return result;
}
