// Included by electronics_modeling.cpp in its private native namespace.
constexpr double cableSweepTolerance035B=1e-6;
using CablePoles035B=std::vector<gp_XYZ>;
gp_XYZ cableBezierValue035B(CablePoles035B values){
    for(size_t n=values.size();n>1;--n)for(size_t i=0;i+1<n;++i)values[i]=(values[i]+values[i+1])*.5;
    return values.front();
}
std::pair<CablePoles035B,CablePoles035B> cableBezierHalves035B(CablePoles035B work){
    CablePoles035B left{work.front()},right{work.back()};
    for(size_t n=work.size();n>1;--n){for(size_t i=0;i+1<n;++i)work[i]=(work[i]+work[i+1])*.5;left.push_back(work.front());right.push_back(work[n-2]);}
    std::reverse(right.begin(),right.end());return {left,right};
}
struct CableCurvature035B {double upper=0,sampled=0;size_t leaves=0,visits=0;std::vector<CablePoles035B> injectiveSpans;};
void boundCableCurvature035B(const CablePoles035B& p,double requiredRadius,int depth,CableCurvature035B& receipt){
    if(++receipt.visits>65536)throw std::runtime_error("CABLE_CURVATURE_UNPROVEN: bounded Bezier subdivision capacity exceeded");
    const size_t degree=p.size()-1;CablePoles035B velocity,acceleration;
    for(size_t i=0;i<degree;++i)velocity.push_back((p[i+1]-p[i])*(double)degree);
    if(velocity.empty())throw std::runtime_error("CABLE_PATH_DEGENERATE: spline has no derivative");
    for(size_t i=0;i+1<velocity.size();++i)acceleration.push_back((velocity[i+1]-velocity[i])*(double)(degree-1));
    const gp_XYZ centerVelocity=cableBezierValue035B(velocity);const double centerSpeed=centerVelocity.Modulus();
    if(centerSpeed<1e-14)throw std::runtime_error("CABLE_PATH_DEGENERATE: spline has a stationary derivative");
    const gp_XYZ centerAcceleration=acceleration.empty()?gp_XYZ(0,0,0):cableBezierValue035B(acceleration);
    const double sampled=centerVelocity.Crossed(centerAcceleration).Modulus()/(centerSpeed*centerSpeed*centerSpeed);
    receipt.sampled=std::max(receipt.sampled,sampled);
    const double limit=1/requiredRadius;
    if(!std::isfinite(sampled)||sampled>limit)throw std::runtime_error("CABLE_BEND_TOO_SHARP: native spline curvature exceeds the authored minimum bend radius");
    const gp_XYZ direction=centerVelocity/centerSpeed;double lower=1e300,maxVelocity=0,maxAcceleration=0,crossUpper=0;
    for(const auto& v:velocity){lower=std::min(lower,v.Dot(direction));maxVelocity=std::max(maxVelocity,v.Modulus());}
    for(const auto& a:acceleration){maxAcceleration=std::max(maxAcceleration,a.Modulus());for(const auto& v:velocity)crossUpper=std::max(crossUpper,v.Crossed(a).Modulus());}
    // Derivatives are in their Bezier control hulls. The positive projection
    // bounds speed from below; every pair of derivative hull vertices bounds
    // |C' x C''| above. Fixed floating-point guards make acceptance conservative.
    lower-=maxVelocity*1e-12;crossUpper+=maxVelocity*maxAcceleration*1e-12;
    const double upper=lower>0?crossUpper/(lower*lower*lower):1e300;
    double lengthUpper=0;for(size_t i=1;i<p.size();++i)lengthUpper+=(p[i]-p[i-1]).Modulus();
    // A leaf also bounds its total turning below pi/4. Together with the
    // profile extent being strictly below the curvature-radius bound, this
    // excludes a local fold inside one inspection/sweep edge. Remote overlap
    // remains a native BOP check between the resulting distinct lateral faces.
    if(std::isfinite(upper)&&upper<=limit&&upper*lengthUpper<=std::acos(-1.)/4){
        if(receipt.leaves>=512)throw std::runtime_error("CABLE_SELF_INTERSECTION_UNPROVEN: native injective-span inspection capacity exceeded");
        receipt.upper=std::max(receipt.upper,upper);++receipt.leaves;receipt.injectiveSpans.push_back(p);return;
    }
    if(depth>=14)throw std::runtime_error("CABLE_CURVATURE_UNPROVEN: spline regularity or minimum bend radius could not be bounded");
    const auto halves=cableBezierHalves035B(p);boundCableCurvature035B(halves.first,requiredRadius,depth+1,receipt);boundCableCurvature035B(halves.second,requiredRadius,depth+1,receipt);
}
void cableSelfInterference035B(const TopoDS_Shape& shape,const char* label){
    BOPAlgo_ArgumentAnalyzer analyzer;analyzer.SetShape1(shape);analyzer.SelfInterMode()=true;analyzer.ArgumentTypeMode()=false;analyzer.SmallEdgeMode()=false;analyzer.RebuildFaceMode()=false;analyzer.TangentMode()=false;analyzer.MergeVertexMode()=false;analyzer.MergeEdgeMode()=false;analyzer.ContinuityMode()=false;analyzer.CurveOnSurfaceMode()=false;analyzer.StopOnFirstFaulty()=true;analyzer.SetRunParallel(false);analyzer.Perform();
    if(analyzer.HasErrors()||analyzer.HasFaulty())throw std::runtime_error(std::string("CABLE_SELF_INTERSECTION: ")+label+" has native self-interference");
}
V cableFrame035B(const gp_Pnt& origin,const gp_Vec& tangent,const gp_Vec& width,const gp_Vec& thickness){
    V row=V::makeObj();row.set("origin_mm",vec(origin.XYZ()));row.set("tangent",vec(tangent.Normalized().XYZ()));row.set("width_direction",vec(width.Normalized().XYZ()));row.set("thickness_direction",vec(thickness.Normalized().XYZ()));return row;
}
struct CableCirclePole035B {gp_XYZ numerator;double weight;};
double cableBinomial035B(int n,int i){double value=1;for(int k=1;k<=i;++k)value*=double(n-k+1)/k;return value;}
void checkCircularCableBezier035B(const std::vector<CableCirclePole035B>& poles,const gp_Vec& normal,double radius,int depth,size_t& visits){
    if(++visits>4096)throw std::runtime_error("CABLE_FRAME_INVALID: terminal circle verification capacity exceeded");
    const int degree=(int)poles.size()-1;double minWeight=1e300,planeResidual=0,radialResidual=0;
    for(const auto& p:poles){minWeight=std::min(minWeight,p.weight);planeResidual=std::max(planeResidual,std::abs(p.numerator.Dot(normal.XYZ())));}
    if(minWeight<1e-12)throw std::runtime_error("CABLE_FRAME_INVALID: terminal circular profile has unsupported weights");
    // Bernstein product coefficients bound the complete rational curve's
    // implicit circle residual, not just a set of sampled boundary points.
    for(int k=0;k<=2*degree;++k){double coefficient=0;for(int i=std::max(0,k-degree);i<=std::min(degree,k);++i){const int j=k-i;coefficient+=cableBinomial035B(degree,i)*cableBinomial035B(degree,j)/cableBinomial035B(2*degree,k)*(poles[i].numerator.Dot(poles[j].numerator)-radius*radius*poles[i].weight*poles[j].weight);}radialResidual=std::max(radialResidual,std::abs(coefficient));}
    if(planeResidual<=cableSweepTolerance035B*minWeight&&radialResidual<=radius*cableSweepTolerance035B*minWeight*minWeight)return;
    if(depth>=12)throw std::runtime_error("CABLE_FRAME_INVALID: native terminal circle or plane could not be verified at fixed tolerance");
    auto work=poles;std::vector<CableCirclePole035B> a{work.front()},b{work.back()};for(size_t n=work.size();n>1;--n){for(size_t i=0;i+1<n;++i){work[i].numerator=(work[i].numerator+work[i+1].numerator)*.5;work[i].weight=(work[i].weight+work[i+1].weight)*.5;}a.push_back(work.front());b.push_back(work[n-2]);}std::reverse(b.begin(),b.end());checkCircularCableBezier035B(a,normal,radius,depth+1,visits);checkCircularCableBezier035B(b,normal,radius,depth+1,visits);
}
void checkCircularCableSection035B(const TopoDS_Wire& wire,const gp_Pnt& center,const gp_Vec& tangent,double radius){
    size_t edges=0,visits=0;for(BRepTools_WireExplorer walk(wire);walk.More();walk.Next()){
        if(++edges>16)throw std::runtime_error("CABLE_FRAME_INVALID: terminal circular profile exceeds bounded edge count");
        BRepAdaptor_Curve edge(walk.Current());
        if(edge.GetType()==GeomAbs_Circle){const gp_Circ circle=edge.Circle();const double offset=circle.Location().Distance(center),planeBound=offset+circle.Radius()*gp_Vec(circle.Axis().Direction()).Crossed(tangent).Magnitude();if(offset+std::abs(circle.Radius()-radius)>cableSweepTolerance035B||planeBound>cableSweepTolerance035B)throw std::runtime_error("CABLE_FRAME_INVALID: analytic terminal circle changed center, radius or normal");continue;}
        if(edge.GetType()!=GeomAbs_BSplineCurve)throw std::runtime_error("CABLE_FRAME_INVALID: terminal circular profile is not a supported native circle or BSpline");
        const Handle(Geom_BSplineCurve) curve=edge.BSpline();if(curve->Degree()>16||curve->NbPoles()>256)throw std::runtime_error("CABLE_FRAME_INVALID: terminal circular profile exceeds bounded native degree or poles");GeomConvert_BSplineCurveToBezierCurve spans(curve);if(spans.NbArcs()>64)throw std::runtime_error("CABLE_FRAME_INVALID: terminal circular profile exceeds bounded spans");
        for(int i=1;i<=spans.NbArcs();++i){const Handle(Geom_BezierCurve) arc=spans.Arc(i);std::vector<CableCirclePole035B> poles;for(int k=1;k<=arc->NbPoles();++k){const double w=arc->Weight(k);poles.push_back({(arc->Pole(k).XYZ()-center.XYZ())*w,w});}checkCircularCableBezier035B(poles,tangent,radius,0,visits);}
    }
    if(!edges)throw std::runtime_error("CABLE_FRAME_INVALID: empty native terminal profile");
}
V cableTerminalFrame035B(const TopoDS_Shape& section,const gp_Pnt& expected,const gp_Vec& tangent,bool ribbon,double width,double thickness,double radius){
    if(section.IsNull()||section.ShapeType()!=TopAbs_WIRE)throw std::runtime_error("CABLE_FRAME_INVALID: sweep terminal section is not a wire");
    const TopoDS_Wire wire=TopoDS::Wire(section);gp_Vec x,y;gp_Pnt center;
    if(ribbon){
        std::vector<gp_Pnt> corners;for(BRepTools_WireExplorer walk(wire);walk.More();walk.Next())corners.push_back(BRep_Tool::Pnt(walk.CurrentVertex()));
        if(corners.size()!=4)throw std::runtime_error("CABLE_FRAME_INVALID: ribbon terminal section lost its four corners");
        center=gp_Pnt((corners[0].XYZ()+corners[1].XYZ()+corners[2].XYZ()+corners[3].XYZ())*.25);
        x=gp_Vec(corners[0],corners[1]);y=gp_Vec(corners[1],corners[2]);
        if(std::abs(x.Magnitude()-width)>cableSweepTolerance035B||std::abs(y.Magnitude()-thickness)>cableSweepTolerance035B)throw std::runtime_error("CABLE_FRAME_INVALID: transported ribbon profile dimensions changed");
    }else{
        BRepTools_WireExplorer walk(wire);if(!walk.More())throw std::runtime_error("CABLE_FRAME_INVALID: empty round terminal section");
        checkCircularCableSection035B(wire,expected,tangent.Normalized(),radius);BRepAdaptor_Curve edge(walk.Current());center=expected;x=gp_Vec(center,edge.Value(edge.FirstParameter()));x-=tangent.Normalized()*x.Dot(tangent.Normalized());y=tangent.Normalized().Crossed(x);
    }
    if(center.Distance(expected)>cableSweepTolerance035B)throw std::runtime_error("CABLE_FRAME_INVALID: transported endpoint does not match its authored anchor");
    x.Normalize();y.Normalize();const gp_Vec t=tangent.Normalized();
    if(std::abs(x.Dot(y))>1e-6||std::abs(x.Dot(t))>1e-6||std::abs(y.Dot(t))>1e-6)throw std::runtime_error("CABLE_FRAME_INVALID: terminal frame is not orthonormal to the route");
    if(x.Crossed(y).Dot(t)<0)y.Reverse();return cableFrame035B(center,t,x,y);
}
V modelSplineCable035B(const V& request){
    const V& path=sequence(request,"path",34);if(path.arr.size()<2)throw std::runtime_error("CABLE_PATH_DEGENERATE: spline needs at least two endpoints");
    Handle(TColgp_HArray1OfPnt) anchors=new TColgp_HArray1OfPnt(1,(int)path.arr.size());
    for(size_t i=0;i<path.arr.size();++i){const gp_Pnt p=xyz(path.arr[i]);if(std::max({std::abs(p.X()),std::abs(p.Y()),std::abs(p.Z())})>10000)throw std::runtime_error("Cable coordinates must stay within 10 m");for(size_t j=0;j<i;++j)if(p.Distance(anchors->Value((int)j+1))<1e-4)throw std::runtime_error("CABLE_PATH_DEGENERATE: repeated or coincident spline anchors");anchors->SetValue((int)i+1,p);}
    const V& profile=need(request,"profile");const std::string type=word(profile,"type");const bool ribbon=type=="ribbon";
    if(!ribbon&&type!="round")throw std::runtime_error("CABLE_PROFILE_INVALID: unsupported spline cross section");
    const double width=ribbon?number(profile,"width"):0,thickness=ribbon?number(profile,"thickness"):0,radius=ribbon?0:number(profile,"radius");
    if((ribbon&&(width<1e-4||width>1000||thickness<1e-4||thickness>100||thickness>width))||(!ribbon&&(radius<1e-4||radius>100)))throw std::runtime_error("CABLE_PROFILE_INVALID: invalid spline cross-section dimensions");
    const double extent=ribbon?std::hypot(width,thickness)/2:radius,bend=number(request,"bend_radius");if(bend<=extent||bend>1e5)throw std::runtime_error("CABLE_BEND_INVALID: bend radius must exceed the profile circumradius");
    GeomAPI_Interpolate interpolate(anchors,false,1e-7);
    const auto* initial=request.find("start_tangent"),*final=request.find("end_tangent");if((initial!=nullptr)!=(final!=nullptr))throw std::runtime_error("CABLE_FRAME_INVALID: endpoint tangents must be paired");
    if(initial){const gp_Vec a(xyz(*initial).XYZ()),b(xyz(*final).XYZ());if(a.Magnitude()<1e-8||b.Magnitude()<1e-8)throw std::runtime_error("CABLE_FRAME_INVALID: endpoint tangents must be nonzero");interpolate.Load(a,b,true);}
    interpolate.Perform();if(!interpolate.IsDone())throw std::runtime_error("CABLE_SPLINE_FAILED: native interpolation failed");const Handle(Geom_BSplineCurve) curve=interpolate.Curve();
    if(curve->IsRational()||curve->Degree()>3||curve->NbPoles()>102)throw std::runtime_error("CABLE_SPLINE_UNSUPPORTED: interpolation exceeds bounded nonrational cubic domain");
    V centerline=V::makeObj(),poles=V::makeArr(),knots=V::makeArr(),multiplicities=V::makeArr();
    for(int i=1;i<=curve->NbPoles();++i){const gp_Pnt p=curve->Pole(i);if(std::max({std::abs(p.X()),std::abs(p.Y()),std::abs(p.Z())})>10000)throw std::runtime_error("CABLE_SPLINE_UNSUPPORTED: spline control hull exceeds the finite 10 m domain");poles.arr.push_back(vec(p.XYZ()));}
    for(int i=1;i<=curve->NbKnots();++i){knots.arr.push_back(V::makeNum(curve->Knot(i)));multiplicities.arr.push_back(V::makeNum(curve->Multiplicity(i)));}
    centerline.set("kind",V::makeStr("nonrational_bspline"));centerline.set("degree",V::makeNum(curve->Degree()));centerline.set("poles_mm",poles);centerline.set("knots",knots);centerline.set("multiplicities",multiplicities);centerline.set("interpolates_authored_anchors",V::makeBool(true));
    GeomConvert_BSplineCurveToBezierCurve beziers(curve);if(beziers.NbArcs()>33)throw std::runtime_error("CABLE_SPLINE_UNSUPPORTED: too many native spline spans");CableCurvature035B curvature;
    for(int i=1;i<=beziers.NbArcs();++i){const Handle(Geom_BezierCurve) arc=beziers.Arc(i);CablePoles035B p;for(int k=1;k<=arc->NbPoles();++k)p.push_back(arc->Pole(k).XYZ());boundCableCurvature035B(p,bend,0,curvature);}
    // A positive derivative projection proves each accepted leaf injective.
    // Native intersections between these separate exact Bezier edges therefore
    // also cover loops contained inside one original interpolation span.
    BRepBuilderAPI_MakeWire checkSpine;
    for(const auto& leaf:curvature.injectiveSpans){NCollection_Array1<gp_Pnt> p(1,(int)leaf.size());for(size_t i=0;i<leaf.size();++i)p.SetValue((int)i+1,gp_Pnt(leaf[i]));const Handle(Geom_BezierCurve) arc=new Geom_BezierCurve(p);checkSpine.Add(BRepBuilderAPI_MakeEdge(arc).Edge());}
    if(!checkSpine.IsDone())throw std::runtime_error("CABLE_SPLINE_FAILED: Bezier inspection wire did not connect");cableSelfInterference035B(checkSpine.Wire(),"centerline");
    BRepBuilderAPI_MakeEdge edge(curve);if(!edge.IsDone())throw std::runtime_error("CABLE_SPLINE_FAILED: native spline edge failed");BRepBuilderAPI_MakeWire spine(edge.Edge());if(!spine.IsDone())throw std::runtime_error("CABLE_SPLINE_FAILED: native spline wire failed");
    gp_Pnt start,end;gp_Vec startTangent,endTangent;curve->D1(curve->FirstParameter(),start,startTangent);curve->D1(curve->LastParameter(),end,endTangent);
    if(startTangent.Magnitude()<1e-8||endTangent.Magnitude()<1e-8)throw std::runtime_error("CABLE_FRAME_INVALID: spline has a stationary endpoint");startTangent.Normalize();endTangent.Normalize();
    if(start.Distance(anchors->Value(1))>cableSweepTolerance035B||end.Distance(anchors->Value(anchors->Upper()))>cableSweepTolerance035B)throw std::runtime_error("CABLE_FRAME_INVALID: spline failed endpoint interpolation");
    gp_Vec up=ribbon?gp_Vec(xyz(need(profile,"up")).XYZ()):gp_Vec(0,0,1);
    if(!ribbon&&std::abs(up.Dot(startTangent))>.95)up=gp_Vec(0,1,0);
    if(up.Magnitude()<1e-8)throw std::runtime_error("CABLE_FRAME_INVALID: initial profile up is zero");up.Normalize();up-=startTangent*up.Dot(startTangent);
    if(up.Magnitude()<1e-6)throw std::runtime_error("CABLE_FRAME_INVALID: ribbon up is parallel to the actual spline tangent");up.Normalize();const gp_Vec across=up.Crossed(startTangent).Normalized();
    TopoDS_Wire section;
    if(ribbon){BRepBuilderAPI_MakePolygon polygon;for(const auto& q:std::vector<std::pair<double,double>>{{-width/2,-thickness/2},{width/2,-thickness/2},{width/2,thickness/2},{-width/2,thickness/2}})polygon.Add(start.Translated(across*q.first+up*q.second));polygon.Close();if(!polygon.IsDone())throw std::runtime_error("CABLE_PROFILE_INVALID: ribbon profile failed");section=polygon.Wire();}
    else section=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(start,gp_Dir(startTangent),gp_Dir(across)),radius)).Edge()).Wire();
    // Sweep the same exact de Casteljau leaf geometry used by inspection.
    // Keeping those boundaries as native topology exposes remote profile
    // overlaps to BOP instead of hiding them inside one lateral face.
    BRepOffsetAPI_MakePipeShell pipe(checkSpine.Wire());pipe.SetMode(false);pipe.SetTolerance(cableSweepTolerance035B,cableSweepTolerance035B,1e-4);pipe.SetMaxSegments(128);pipe.Add(section,false,false);pipe.Build();
    if(!pipe.IsDone())throw std::runtime_error("CABLE_SWEEP_FAILED: native corrected-Frenet sweep failed");const double sweepError=pipe.ErrorOnSurface();
    if(!std::isfinite(sweepError)||sweepError>cableSweepTolerance035B)throw std::runtime_error("CABLE_SWEEP_UNPROVEN: native surface approximation exceeds fixed 0.000001 mm tolerance");
    const V startFrame=cableTerminalFrame035B(pipe.FirstShape(),start,startTangent,ribbon,width,thickness,radius),endFrame=cableTerminalFrame035B(pipe.LastShape(),end,endTangent,ribbon,width,thickness,radius);
    if(!pipe.MakeSolid())throw std::runtime_error("CABLE_SWEEP_FAILED: native cable sweep could not be capped");const TopoDS_Shape solid=pipe.Shape();
    if(!BRepCheck_Analyzer(solid).IsValid())throw std::runtime_error("CABLE_SWEEP_INVALID: cable failed native B-rep validation");cableSelfInterference035B(solid,"swept profile");
    double nearest=1e300;for(const auto& item:sequence(request,"obstacles",64).arr){const gp_Pnt lo=xyz(need(item,"min")),hi=xyz(need(item,"max"));if(hi.X()<=lo.X()||hi.Y()<=lo.Y()||hi.Z()<=lo.Z())throw std::runtime_error("Cable obstacle bounds invalid");const TopoDS_Shape block=BRepPrimAPI_MakeBox(lo,hi).Shape();BRepExtrema_DistShapeShape distance(solid,block);distance.Perform();if(!distance.IsDone())throw std::runtime_error("CABLE_CLEARANCE_UNPROVEN: native cable-to-AABB distance failed");nearest=std::min(nearest,distance.Value());if(distance.Value()<=cableSweepTolerance035B)throw std::runtime_error("CABLE_OBSTACLE_COLLISION: Exact cable collides with obstacle "+word(item,"name"));}
    GProp_GProps length;BRepGProp::LinearProperties(spine.Wire(),length);if(!std::isfinite(length.Mass())||length.Mass()<=1e-4||length.Mass()>100000)throw std::runtime_error("CABLE_PATH_DEGENERATE: native spline length is outside the supported domain");
    V result=shapeResult(solid,true),report=need(request,"report");report.set("route_length_mm",V::makeNum(length.Mass()));report.set("route_length_pending_native",V::makeBool(false));report.set("length_method",V::makeStr("OCCT native curve integration"));report.set("centerline",centerline);report.set("start_frame",startFrame);report.set("end_frame",endFrame);report.set("frame_transport",V::makeStr("OCCT corrected Frenet, authored initial profile frame"));report.set("bend_radius_checked",V::makeBool(true));report.set("curvature_bound_method",V::makeStr("nonrational Bezier derivative convex hull subdivision with fixed floating-point guards"));report.set("curvature_bound_max_per_mm",V::makeNum(curvature.upper));report.set("curvature_bound_leaves",V::makeNum((double)curvature.leaves));report.set("curvature_sample_max_per_mm",V::makeNum(curvature.sampled));
    if(curvature.upper>0)report.set("minimum_bend_radius_bound_mm",V::makeNum(1/curvature.upper));else report.set("zero_curvature",V::makeBool(true));
    report.set("self_intersections_checked",V::makeBool(true));report.set("self_intersection_method",V::makeStr("OCCT BOP on injective short-turn Bezier leaf wire and its separate swept lateral faces"));report.set("leaf_total_turning_bound_rad",V::makeNum(std::acos(-1.)/4));report.set("exact_obstacle_checked",V::makeBool(true));report.set("obstacle_count",V::makeNum((double)need(request,"obstacles").arr.size()));if(nearest<1e299)report.set("minimum_obstacle_clearance_mm",V::makeNum(nearest));
    report.set("sweep_surface_error_mm",V::makeNum(sweepError));report.set("sweep_tolerance_mm",V::makeNum(cableSweepTolerance035B));report.set("manufacturing_qualified",V::makeBool(false));report.set("dynamic_physics",V::makeBool(false));report.set("route_scope",V::makeStr("native 3D interpolating spline and constant profile sweep; bounded curvature and B-rep self-interference, static declared/occurrence AABB clearance; no electrical continuity, connector fit, material strain, dynamic motion or manufacturing qualification"));
    V geometry=need(result,"report");geometry.set("self_intersections_checked",V::makeBool(true));geometry.set("self_intersection_scope",V::makeStr("OCCT native BOP argument analyzer; no tessellation authority"));result.set("report",geometry);result.set("electronics",report);result.set("electronics_kind",V::makeStr("cable_route"));return result;
}
