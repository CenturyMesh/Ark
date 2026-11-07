// Exact compatible polynomial curve networks and read-only loft correspondence.
// Included in surfacing.cpp's private namespace after surfaceEditResult.
void networkNeed068(bool ok,const std::string& reason){if(!ok)throw std::runtime_error("SURFACE_NETWORK: "+reason);}
void networkKeys068(const V& object,std::initializer_list<const char*> keys){networkNeed068(object.t==V::Obj,"Expected object");for(const auto& item:object.obj)networkNeed068(std::any_of(keys.begin(),keys.end(),[&](const char* key){return item.first==key;}),"Unknown field: "+item.first);}
std::vector<double> networkStations068(const V& request,const char* key,size_t count){
    std::vector<double> out;const auto* authored=request.find(key);
    if(authored){networkNeed068(authored->t==V::Arr&&authored->arr.size()==count,std::string(key)+" must match the transverse curve count");for(const auto& x:authored->arr)out.push_back(number(x,key));}
    else for(size_t i=0;i<count;++i)out.push_back(double(i)/(count-1));
    networkNeed068(out.front()==0&&out.back()==1,std::string(key)+" must start at 0 and end at 1");
    for(size_t i=1;i<count;++i)networkNeed068(out[i]>out[i-1]+1e-5&&out[i]<=1,"Stations must be strictly increasing and separated by more than 1e-5");
    return out;
}
bool networkReverse068(const V& request,const char* key,size_t index,size_t count){const auto* a=request.find(key);if(!a)return false;networkNeed068(a->t==V::Arr&&a->arr.size()==count,std::string(key)+" must match family count");for(const auto& v:a->arr)networkNeed068(v.t==V::Bool,"Curve reversal must be boolean");return a->arr[index].b;}
Handle(Geom_BezierCurve) networkCurve068(const V& encoded,bool reverse){
    networkNeed068(encoded.t==V::Str,"Curve B-rep must be text");const auto shape=readBrep(encoded.str);const auto edges=shapes(shape,TopAbs_EDGE);
    networkNeed068((shape.ShapeType()==TopAbs_WIRE||shape.ShapeType()==TopAbs_EDGE)&&shapes(shape,TopAbs_FACE).empty()&&edges.size()==1,"Each network curve must contain one exact open polynomial edge");
    const auto edge=TopoDS::Edge(edges.front());BRepAdaptor_Curve adaptor(edge);networkNeed068(!adaptor.IsClosed()&&!BRep_Tool::Degenerated(edge),"Closed or degenerate network edges are unsupported");
    Handle(Geom_BezierCurve) curve;
    if(adaptor.GetType()==GeomAbs_Line){NCollection_Array1<gp_Pnt> poles(1,2);poles(1)=adaptor.Value(adaptor.FirstParameter());poles(2)=adaptor.Value(adaptor.LastParameter());curve=new Geom_BezierCurve(poles);}
    else{
        double first,last;TopLoc_Location location;auto original=BRep_Tool::Curve(edge,location,first,last);networkNeed068(!original.IsNull(),"Missing native curve");
        Handle(Geom_TrimmedCurve) trimmed=new Geom_TrimmedCurve(original,first,last);auto spline=GeomConvert::CurveToBSplineCurve(trimmed);
        networkNeed068(!spline.IsNull()&&!spline->IsRational()&&!spline->IsPeriodic()&&spline->NbKnots()==2,"Network currently requires non-rational single-span polynomial curves");
        networkNeed068(spline->Degree()>=1&&spline->Degree()<=8&&spline->NbPoles()==spline->Degree()+1,"Network curve degree must be 1..8 with a clamped single-span basis");
        NCollection_Array1<gp_Pnt> poles(1,spline->NbPoles());for(int i=1;i<=spline->NbPoles();++i)poles(i)=spline->Pole(i).Transformed(location.Transformation());curve=new Geom_BezierCurve(poles);
    }
    if((edge.Orientation()==TopAbs_REVERSED)!=reverse)curve->Reverse();return curve;
}
double choose068(int n,int k){if(k<0||k>n)return 0;double r=1;for(int i=1;i<=k;++i)r*=double(n-k+i)/i;return r;}
// Lagrange interpolation expressed exactly in the common Bernstein basis.
std::vector<std::vector<double>> networkBasis068(const std::vector<double>& stations,int degree){
    std::vector<std::vector<double>> basis;
    for(size_t j=0;j<stations.size();++j){std::vector<double> power{1};for(size_t k=0;k<stations.size();++k)if(k!=j){const double denominator=stations[j]-stations[k];std::vector<double> next(power.size()+1,0);for(size_t i=0;i<power.size();++i){next[i]-=power[i]*stations[k]/denominator;next[i+1]+=power[i]/denominator;}power=std::move(next);}
        std::vector<double> b(degree+1,0);for(int i=0;i<=degree;++i)for(int k=0;k<=i&&k<(int)power.size();++k)b[i]+=power[k]*choose068(i,k)/choose068(degree,k);basis.push_back(std::move(b));}
    return basis;
}
V surfaceNetwork068(const V& request){
    networkKeys068(request,{"op","input_breps","u_count","u_stations","v_stations","u_reverse","v_reverse","tolerance"});tolerance(request);
    const auto& encoded=required(request,"input_breps");networkNeed068(encoded.t==V::Arr,"input_breps must be an ordered array");const int nu=integer(required(request,"u_count"),"u_count",2,9),nv=(int)encoded.arr.size()-nu;networkNeed068(nv>=2&&nv<=9,"Network requires 2..9 curves in each family");
    const auto us=networkStations068(request,"u_stations",nv),vs=networkStations068(request,"v_stations",nu);
    std::vector<Handle(Geom_BezierCurve)> uc,vc;int degreeU=std::max(3,nv-1),degreeV=std::max(3,nu-1);
    for(int i=0;i<nu;++i){uc.push_back(networkCurve068(encoded.arr[i],networkReverse068(request,"u_reverse",i,nu)));degreeU=std::max(degreeU,uc.back()->Degree());}
    for(int i=0;i<nv;++i){vc.push_back(networkCurve068(encoded.arr[nu+i],networkReverse068(request,"v_reverse",i,nv)));degreeV=std::max(degreeV,vc.back()->Degree());}
    std::vector<std::vector<gp_Pnt>> cross(nv,std::vector<gp_Pnt>(nu));double crossingError=0;
    for(int i=0;i<nv;++i)for(int j=0;j<nu;++j){const auto a=uc[j]->Value(us[i]),b=vc[i]->Value(vs[j]);const auto gap=a.Distance(b);crossingError=std::max(crossingError,gap);networkNeed068(gap<=kTolerance,"Curves do not intersect at the authored normalized stations (U "+std::to_string(j)+", V "+std::to_string(i)+", gap "+std::to_string(gap)+" mm)");cross[i][j]=a;}
    for(auto& c:uc)c->Increase(degreeU);for(auto& c:vc)c->Increase(degreeV);
    const auto ub=networkBasis068(us,degreeU),vb=networkBasis068(vs,degreeV);
    NCollection_Array2<gp_Pnt> poles(1,degreeU+1,1,degreeV+1);
    for(int a=0;a<=degreeU;++a)for(int b=0;b<=degreeV;++b){gp_XYZ p(0,0,0);for(int j=0;j<nu;++j)p+=uc[j]->Pole(a+1).XYZ()*vb[j][b];for(int i=0;i<nv;++i)p+=vc[i]->Pole(b+1).XYZ()*ub[i][a];for(int i=0;i<nv;++i)for(int j=0;j<nu;++j)p-=cross[i][j].XYZ()*(ub[i][a]*vb[j][b]);networkNeed068(std::isfinite(p.X())&&std::isfinite(p.Y())&&std::isfinite(p.Z()),"Non-finite Gordon coefficient");poles(a+1,b+1)=gp_Pnt(p);}
    Handle(Geom_BezierSurface) surface=new Geom_BezierSurface(poles);double error=0;unsigned count=0;
    for(int j=0;j<nu;++j)for(int k=0;k<=64;++k){const double u=k/64.;error=std::max(error,surface->Value(u,vs[j]).Distance(uc[j]->Value(u)));++count;}
    for(int i=0;i<nv;++i)for(int k=0;k<=64;++k){const double v=k/64.;error=std::max(error,surface->Value(us[i],v).Distance(vc[i]->Value(v)));++count;}
    networkNeed068(error<=kTolerance,"Gordon result fails native curve preservation checkpoints");
    double minJacobian=1e300;for(int i=0;i<=16;++i)for(int j=0;j<=16;++j){gp_Pnt p;gp_Vec du,dv;surface->D1(i/16.,j/16.,p,du,dv);const double jac=du.Crossed(dv).Magnitude();networkNeed068(std::isfinite(jac)&&jac>1e-10,"Network has an observed singular or folded parameter region");minJacobian=std::min(minJacobian,jac);}
    const auto editableBasis=GeomConvert::SurfaceToBSplineSurface(surface);networkNeed068(!editableBasis.IsNull(),"Exact polynomial basis conversion failed");BRepBuilderAPI_MakeFace maker(editableBasis,kTolerance);networkNeed068(maker.IsDone()&&BRepCheck_Analyzer(maker.Face()).IsValid(),"Network produced an invalid B-rep face");BRepAlgoAPI_Check interference(maker.Face());networkNeed068(interference.IsValid(),"Network failed native self-interference check");
    V out=surfaceEditResult(maker.Face(),"curve_network"),report=required(out,"report");report.set("construction",V::makeStr("exact_polynomial_Gordon_Boolean_sum"));report.set("u_curves",V::makeNum(nu));report.set("v_curves",V::makeNum(nv));report.set("u_degree",V::makeNum(degreeU));report.set("v_degree",V::makeNum(degreeV));report.set("crossing_max_gap_mm",V::makeNum(crossingError));report.set("curve_observed_max_error_mm",V::makeNum(error));report.set("curve_checkpoints",V::makeNum(count));report.set("min_sampled_jacobian",V::makeNum(minJacobian));report.set("curve_basis_identity",V::makeBool(crossingError==0));report.set("global_injectivity_verified",V::makeBool(false));report.set("authority",V::makeStr("native polynomial curves and Bernstein coefficients; no mesh fitting"));out.set("report",report);return out;
}

std::vector<gp_Pnt> loftSamples068(const Profile& section){
    std::vector<double> lengths;double total=0;for(const auto& edge:section.edges){BRepAdaptor_Curve c(edge.edge);const double length=GCPnts_AbscissaPoint::Length(c,1e-9);networkNeed068(length>1e-9,"Degenerate loft section edge");lengths.push_back(length);total+=length;}
    std::vector<gp_Pnt> points;const int samples=section.closed?32:33;
    for(int k=0;k<samples;++k){const double distance=total*k/32.;double accumulated=0;size_t edge=0;while(edge+1<lengths.size()&&distance>accumulated+lengths[edge])accumulated+=lengths[edge++];BRepAdaptor_Curve c(section.edges[edge].edge);const bool reversed=section.edges[edge].edge.Orientation()==TopAbs_REVERSED;const double from=reversed?c.LastParameter():c.FirstParameter(),to=reversed?c.FirstParameter():c.LastParameter();const double travel=std::clamp(distance-accumulated,0.,lengths[edge]);double parameter=from;if(travel>=lengths[edge]-1e-9)parameter=to;else if(travel>1e-9){GCPnts_AbscissaPoint solve(1e-9,c,(reversed?-1:1)*travel,from);networkNeed068(solve.IsDone(),"Loft arc-length correspondence failed");parameter=solve.Parameter();}points.push_back(c.Value(parameter));}return points;
}
V loftDiagnostics068(const V& request){
    validateLoftControls(request);const auto& profiles=required(request,"profiles");networkNeed068(profiles.t==V::Arr&&profiles.arr.size()>=2&&profiles.arr.size()<=32,"Loft needs 2..32 sections");
    V out=V::makeObj(),rows=V::makeArr(),pairs=V::makeArr();std::vector<Profile> sections;std::vector<size_t> indices;bool compatible=true;
    for(size_t i=0;i<profiles.arr.size();++i){V row=V::makeObj();row.set("index",V::makeNum(i));if(pointProfile(profiles.arr[i])){row.set("point_tip",V::makeBool(true));networkNeed068(i==0||i+1==profiles.arr.size(),"Point tips must be first or last");rows.arr.push_back(row);continue;}auto section=alignedSection(request,i,false);row.set("edge_count",V::makeNum(section.edges.size()));row.set("closed",V::makeBool(section.closed));row.set("start_mm",vec(section.edges.front().first.XYZ()));row.set("end_mm",vec(section.edges.back().last.XYZ()));row.set("section_alignment_applied",V::makeBool(request.find("controls")&&request.find("controls")->find("section_alignment")));rows.arr.push_back(row);sections.push_back(std::move(section));indices.push_back(i);}
    for(size_t i=1;i<sections.size();++i){V pair=V::makeObj();const bool same=sections[i-1].closed==sections[i].closed&&sections[i-1].edges.size()==sections[i].edges.size();compatible&=same;pair.set("from",V::makeNum(indices[i-1]));pair.set("to",V::makeNum(indices[i]));pair.set("same_topology",V::makeBool(same));if(sections[i-1].closed==sections[i].closed){const auto a=loftSamples068(sections[i-1]),b=loftSamples068(sections[i]);double direct=0,reverse=0,maxConnector=0;for(size_t k=0;k<a.size();++k){const double distance=a[k].Distance(b[k]);direct+=distance*distance;maxConnector=std::max(maxConnector,distance);const size_t opposite=sections[i].closed?(b.size()-k)%b.size():b.size()-1-k;const double other=a[k].Distance(b[opposite]);reverse+=other*other;}pair.set("correspondence_rms_mm",V::makeNum(std::sqrt(direct/a.size())));pair.set("reversed_rms_mm",V::makeNum(std::sqrt(reverse/a.size())));pair.set("maximum_connector_mm",V::makeNum(maxConnector));pair.set("review_reversal",V::makeBool(reverse+1e-10<direct*.9));pair.set("sample_count",V::makeNum(a.size()));}pairs.arr.push_back(pair);}
    out.set("sections",rows);out.set("pairs",pairs);out.set("same_topology",V::makeBool(compatible));out.set("automatic_changes",V::makeBool(false));out.set("scope",V::makeStr("Native arc-length samples diagnose correspondence; lower reversed distance is a review suggestion, not proof of twist or permission to reverse. Final loft/guide validation remains mandatory."));return out;
}
