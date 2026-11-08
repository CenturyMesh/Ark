// Opt-in exact basis compatibility. No fitting, knot removal or tolerance change.
struct Homogeneous074 {
    gp_XYZ xyz{0,0,0};double w=0;
    Homogeneous074& operator+=(const Homogeneous074& other){xyz+=other.xyz;w+=other.w;return *this;}
    Homogeneous074& operator-=(const Homogeneous074& other){xyz-=other.xyz;w-=other.w;return *this;}
};
Homogeneous074 homogeneous074(const gp_Pnt& p,double w){return {p.XYZ()*w,w};}
Homogeneous074 scaled074(Homogeneous074 value,double factor){value.xyz*=factor;value.w*=factor;return value;}
bool exactCompatible074(const V& request){
    const auto* mode=request.find("basis_mode");if(!mode)return false;
    qualityNeed068(mode->t==V::Str&&(mode->str=="preserve"||mode->str=="exact_compatible"),"basis_mode must be preserve or exact_compatible");return mode->str=="exact_compatible";
}
struct CompatibleFace074 {SurfaceControls059 controls;std::array<double,4> originalBounds{};bool trimmed=false;};
CompatibleFace074 compatibleFace074(const V& request,bool target=false){
    auto selected=qualityFace068(request,target);const auto source=readBrep(required(request,target?"target_brep":"input_brep").asStr());
    qualityNeed068(target||(shapes(source,TopAbs_SOLID).empty()&&shapes(source,TopAbs_FACE).size()==1),"Match edits one extracted surface face; other source faces cannot be discarded");
    qualityNeed068(qualityRectangle068(selected),"Exact compatible Match requires a rectangular isoparametric outer trim without holes; arbitrary trim remains unchanged");
    Handle(Geom_Surface) basis=selected.surface;if(auto trimmed=Handle(Geom_RectangularTrimmedSurface)::DownCast(basis);!trimmed.IsNull())basis=trimmed->BasisSurface();
    auto surface=Handle(Geom_BSplineSurface)::DownCast(basis);qualityNeed068(!surface.IsNull(),"Exact compatible Match needs an existing BSpline support; no sampled conversion is performed");surface=Handle(Geom_BSplineSurface)::DownCast(surface->Copy());
    CompatibleFace074 out;out.originalBounds={selected.u0,selected.u1,selected.v0,selected.v1};double u0,u1,v0,v1;surface->Bounds(u0,u1,v0,v1);out.trimmed=u0!=selected.u0||u1!=selected.u1||v0!=selected.v0||v1!=selected.v1;
    surface->Segment(selected.u0,selected.u1,selected.v0,selected.v1,0.,0.);
    qualityNeed068(!surface->IsUPeriodic()&&!surface->IsVPeriodic()&&!surface->IsUClosed()&&!surface->IsVClosed(),"Match needs an open clamped patch after exact trim segmentation");
    qualityNeed068(surface->UDegree()<=12&&surface->VDegree()<=12&&surface->NbUPoles()<=128&&surface->NbVPoles()<=128,"Exact compatible Match supports degrees through12 and up to128 poles per direction");
    NCollection_Array1<double> uk(1,surface->NbUKnots()),vk(1,surface->NbVKnots());surface->Bounds(u0,u1,v0,v1);
    for(int i=1;i<=uk.Length();++i)uk(i)=(surface->UKnot(i)-u0)/(u1-u0);for(int i=1;i<=vk.Length();++i)vk(i)=(surface->VKnot(i)-v0)/(v1-v0);surface->SetUKnots(uk);surface->SetVKnots(vk);
    qualityNeed068(surface->UMultiplicity(1)==surface->UDegree()+1&&surface->UMultiplicity(surface->NbUKnots())==surface->UDegree()+1&&surface->VMultiplicity(1)==surface->VDegree()+1&&surface->VMultiplicity(surface->NbVKnots())==surface->VDegree()+1,"Exact segmented support did not retain clamped boundaries");
    out.controls={selected.face,surface,selected.hash,selected.reference,0,1,0,1};return out;
}
using KnotBasis074=std::map<double,int>;
KnotBasis074 surfaceTangentKnots074(const Handle(Geom_BSplineSurface)& surface,int side,bool reverse=false){
    KnotBasis074 result;const int count=side<2?surface->NbVKnots():surface->NbUKnots();for(int i=1;i<=count;++i){const double knot=side<2?surface->VKnot(i):surface->UKnot(i);result[reverse?1-knot:knot]=side<2?surface->VMultiplicity(i):surface->UMultiplicity(i);}return result;
}
void insertSurfaceTangent074(const Handle(Geom_BSplineSurface)& surface,int side,const KnotBasis074& basis,bool reverse){
    for(const auto& [value,multiplicity]:basis)if(value>0&&value<1){const double parameter=reverse?1-value:value;if(side<2)surface->InsertVKnot(parameter,multiplicity,0.,false);else surface->InsertUKnot(parameter,multiplicity,0.,false);}
    qualityNeed068(surface->NbUPoles()<=128&&surface->NbVPoles()<=128,"Exact compatible basis exceeds128 poles per direction; no knots were removed or fitted");
}
std::array<std::array<double,3>,3> homogeneousCoefficients074(const SurfaceControls059& source,int side){
    auto polynomial=source;polynomial.surface=Handle(Geom_BSplineSurface)::DownCast(source.surface->Copy());for(int u=1;u<=polynomial.surface->NbUPoles();++u)for(int v=1;v<=polynomial.surface->NbVPoles();++v)polynomial.surface->SetWeight(u,v,1.);
    return qualityCoefficients068(polynomial,side);
}
V surfaceMatch074B(const V& request){
    deformKeys059(request,{"op","input_brep","topology","reference","source_brep_sha256","target_brep","target_topology","target_reference","target_brep_sha256","source_side","target_side","reverse","continuity","cross_scale","max_displacement_mm","samples","tolerance","basis_mode"});tolerance(request);qualityNeed068(exactCompatible074(request),"Exact compatible mode must be explicitly requested");
    auto source=compatibleFace074(request),target=compatibleFace074(request,true);auto& a=source.controls;auto& b=target.controls;
    qualityNeed068(required(request,"source_brep_sha256").asStr()==a.hash&&required(request,"target_brep_sha256").asStr()==b.hash,"Source or target hash changed; inspect and prepare again");
    const int sa=qualitySide068(request,"source_side"),sb=qualitySide068(request,"target_side"),order=integer(required(request,"continuity"),"continuity",0,2),count=request.find("samples")?integer(required(request,"samples"),"samples",3,129):33;
    if(const auto* flag=request.find("reverse"))qualityNeed068(flag->t==V::Bool,"reverse must be a boolean");const bool reverse=request.find("reverse")&&required(request,"reverse").b;const double maximum=number(required(request,"max_displacement_mm"),"max_displacement_mm"),scale=request.find("cross_scale")?number(required(request,"cross_scale"),"cross_scale"):1.;qualityNeed068(maximum>0&&maximum<=1e5&&scale>0&&scale<=100,"Explicit displacement or cross-scale outside range");
    const auto original=Handle(Geom_BSplineSurface)::DownCast(a.surface->Copy());const auto before=qualitySeamEvidence068(a.surface,b.surface,{0,1,0,1},{0,1,0,1},sa,sb,reverse,count);
    const int tangentDegree=std::max(sa<2?a.surface->VDegree():a.surface->UDegree(),sb<2?b.surface->VDegree():b.surface->UDegree());
    a.surface->IncreaseDegree(sa<2?std::max(a.surface->UDegree(),order+1):tangentDegree,sa<2?tangentDegree:std::max(a.surface->VDegree(),order+1));b.surface->IncreaseDegree(sb<2?b.surface->UDegree():tangentDegree,sb<2?tangentDegree:b.surface->VDegree());
    auto common=surfaceTangentKnots074(a.surface,sa);for(const auto& [knot,multiplicity]:surfaceTangentKnots074(b.surface,sb,reverse))common[knot]=std::max(common[knot],multiplicity);insertSurfaceTangent074(a.surface,sa,common,false);insertSurfaceTangent074(b.surface,sb,common,reverse);
    const auto ak=surfaceTangentKnots074(a.surface,sa),bk=surfaceTangentKnots074(b.surface,sb,reverse);qualityNeed068(ak.size()==bk.size(),"Near-coincident knots did not produce the same exact basis; no knot merging is inferred");auto bi=bk.begin();for(auto ai=ak.begin();ai!=ak.end();++ai,++bi)qualityNeed068(std::abs(ai->first-bi->first)<=1e-12&&ai->second==bi->second,"Refined tangent bases remain incompatible");
    const int at=sa<2?a.surface->NbVPoles():a.surface->NbUPoles(),bt=sb<2?b.surface->NbVPoles():b.surface->NbUPoles(),bn=sb<2?b.surface->NbUPoles():b.surface->NbVPoles();qualityNeed068(at==bt,"Refined boundary pole counts differ");
    const auto ac=homogeneousCoefficients074(a,sa),bc=homogeneousCoefficients074(b,sb);auto changed=Handle(Geom_BSplineSurface)::DownCast(a.surface->Copy());double moved=0,weightRatio=1;
    for(int t=0;t<at;++t){std::array<Homogeneous074,3> desired{},solved{};
        for(int r=0;r<std::min(3,bn);++r){const auto bp=qualityPole068(b.surface,sb,r,reverse?bt-1-t:t);const auto h=homogeneous074(b.surface->Pole(bp.first,bp.second),b.surface->Weight(bp.first,bp.second));for(int d=0;d<3;++d)desired[d]+=scaled074(h,bc[d][r]);}
        desired[1]=scaled074(desired[1],-scale);desired[2]=scaled074(desired[2],scale*scale);
        for(int r=0;r<=order;++r){qualityNeed068(std::abs(ac[r][r])>1e-12,"Refined boundary basis is singular");auto h=desired[r];for(int j=0;j<r;++j)h-=scaled074(solved[j],ac[r][j]);solved[r]=scaled074(h,1/ac[r][r]);qualityNeed068(std::isfinite(solved[r].w)&&solved[r].w>1e-12,"Homogeneous matching needs a nonpositive or singular rational weight; source retained");
            const gp_Pnt point(solved[r].xyz/solved[r].w);qualityNeed068(std::isfinite(point.X())&&std::isfinite(point.Y())&&std::isfinite(point.Z()),"Non-finite matched control point");const auto ap=qualityPole068(a.surface,sa,r,t);const double displacement=a.surface->Pole(ap.first,ap.second).Distance(point);moved=std::max(moved,displacement);qualityNeed068(displacement<=maximum,"Matching exceeds the explicit refined-pole displacement bound");const double ratio=solved[r].w/a.surface->Weight(ap.first,ap.second);weightRatio=std::max({weightRatio,ratio,1/ratio});changed->SetPole(ap.first,ap.second,point,solved[r].w);
        }
    }
    double sampledDisplacement=0,oppositeError=0;for(int u=0;u<=16;++u)for(int v=0;v<=16;++v){const double x=u/16.,y=v/16.;qualityJet068(changed,x,y);sampledDisplacement=std::max(sampledDisplacement,changed->Value(x,y).Distance(original->Value(x,y)));}
    qualityNeed068(sampledDisplacement<=maximum,"Matching exceeds the explicit sampled surface displacement bound");for(int k=0;k<=64;++k){const auto uv=qualitySideUV068(0,1,0,1,sa^1,k/64.);oppositeError=std::max(oppositeError,changed->Value(uv[0],uv[1]).Distance(original->Value(uv[0],uv[1])));}qualityNeed068(oppositeError<=kTolerance,"Exact refinement changed the opposite boundary");
    BRepBuilderAPI_MakeFace maker(changed,0.,1.,0.,1.,kTolerance);qualityNeed068(maker.IsDone(),"Matched face construction failed");auto face=maker.Face();face.Orientation(a.face.Orientation());qualityNeed068(BRepCheck_Analyzer(face).IsValid()&&BRepAlgoAPI_Check(face).IsValid(),"Matched face failed native validity/interference checks");
    const auto after=qualitySeamEvidence068(changed,b.surface,{0,1,0,1},{0,1,0,1},sa,sb,reverse,count);qualityNeed068(required(after,order==0?"sampled_g0":order==1?"sampled_g1":"sampled_g2").b,"Matched native jets failed the requested continuity checkpoints");
    auto result=surfaceEditResult(face,"surface_match_boundary"),report=required(result,"report");report.set("source_brep_sha256",V::makeStr(a.hash));report.set("target_brep_sha256",V::makeStr(b.hash));report.set("requested_continuity",V::makeNum(order));report.set("basis_mode",V::makeStr("exact_compatible"));report.set("basis_preserved",V::makeBool(false));report.set("exact_degree_knot_refinement",V::makeBool(true));report.set("rational_homogeneous_matching",V::makeBool(true));report.set("trim_domain_preserved",V::makeBool(true));report.set("source_rectangular_trimmed",V::makeBool(source.trimmed));report.set("target_rectangular_trimmed",V::makeBool(target.trimmed));report.set("result_u_poles",V::makeNum(changed->NbUPoles()));report.set("result_v_poles",V::makeNum(changed->NbVPoles()));report.set("max_pole_displacement_mm",V::makeNum(moved));report.set("max_sampled_surface_displacement_mm",V::makeNum(sampledDisplacement));report.set("max_weight_ratio",V::makeNum(weightRatio));report.set("opposite_boundary_preserved",V::makeBool(true));report.set("opposite_boundary_max_error_mm",V::makeNum(oppositeError));report.set("before",before);report.set("after",after);report.set("global_continuity_verified",V::makeBool(false));report.set("manufacturing_qualified",V::makeBool(false));report.set("scope",V::makeStr("Exact BSpline segmentation, affine UV normalization, degree elevation and knot insertion; homogeneous boundary jets with positive weights. Rectangular isoparametric trims only; no fit, arbitrary trim reconstruction or between-checkpoint displacement guarantee."));result.set("report",report);return result;
}
