// B.03, 031B. Included inside surfacing.cpp's private namespace.
// All observations below come from native curves/surfaces, never tessellation.
gp_Vec vector031B(const V& value, const char* label) {
    if (value.t != V::Arr || value.arr.size() != 3)
        throw std::runtime_error(std::string(label) + " requires three coordinates");
    return {number(value.arr[0], label), number(value.arr[1], label), number(value.arr[2], label)};
}

TopoDS_Face singleSurface031B(const V& encoded, const char* label) {
    if (encoded.t != V::Str || encoded.str.empty()) throw std::runtime_error(std::string(label) + " requires exact B-rep");
    const auto shape = readBrep(encoded.str);
    const auto faces = shapes(shape, TopAbs_FACE);
    if (faces.size() != 1 || !shapes(shape, TopAbs_SOLID).empty())
        throw std::runtime_error(std::string(label) + " requires one open face; extract a face first");
    if (!BRepCheck_Analyzer(shape).IsValid()) throw std::runtime_error(std::string(label) + " is invalid");
    return TopoDS::Face(faces.front());
}

TopoDS_Face supportFace031B(const V& constraint) {
    V selector = V::makeObj();
    selector.set("input_brep", required(constraint, "support_brep"));
    selector.set("topology", required(constraint, "topology"));
    selector.set("reference", required(constraint, "face"));
    const auto index = static_cast<size_t>(semanticFaceIndexNative(selector));
    const auto faces = shapes(readBrep(required(constraint, "support_brep").str), TopAbs_FACE);
    if (index >= faces.size()) throw std::runtime_error("Support semantic face is outside its B-rep");
    return TopoDS::Face(faces[index]);
}

// Attach a parameterization to a private edge copy. This never edits the source.
TopoDS_Edge edgeOnSupport031B(const TopoDS_Edge& edge, const TopoDS_Face& support, int samples, bool alreadyPrivate=false) {
    double first, last;
    const auto curve = BRep_Tool::Curve(edge, first, last);
    if (curve.IsNull() || !std::isfinite(first) || !std::isfinite(last) || last <= first)
        throw std::runtime_error("Support constraint needs one finite native edge");
    const auto surface = BRep_Tool::Surface(support);
    double tol = kTolerance;
    const auto pcurve = GeomProjLib::Curve2d(curve, first, last, surface, tol);
    if (pcurve.IsNull() || !std::isfinite(tol) || tol > kTolerance)
        throw std::runtime_error("Boundary curve cannot be parameterized on its support at 1e-6 mm");
    for (int i = 0; i < samples; ++i) {
        const double t = first + (last - first) * i / (samples - 1.);
        const auto uv = pcurve->Value(t);
        const auto p = curve->Value(t);
        if (surface->Value(uv.X(), uv.Y()).Distance(p) > kTolerance)
            throw std::runtime_error("Boundary edge is off its support surface at a native checkpoint");
        const BRepClass_FaceClassifier inside(support, uv, kTolerance);
        if (inside.State() != TopAbs_IN && inside.State() != TopAbs_ON)
            throw std::runtime_error("Boundary edge leaves the trimmed support face");
    }
    TopoDS_Edge result=edge;
    if(!alreadyPrivate){BRepBuilderAPI_Copy copy(edge,false,false);result=TopoDS::Edge(copy.Shape());}
    BRep_Builder builder;
    builder.UpdateEdge(result, pcurve, support, kTolerance);
    builder.Range(result, support, first, last);
    return result;
}

struct SurfaceJet031B {
    gp_Pnt point;
    gp_Vec du, dv, duu, dvv, duv, normal;
};

SurfaceJet031B jet031B(const Handle(Geom_Surface)& surface, const gp_Pnt& at) {
    GeomAPI_ProjectPointOnSurf projection(at, surface, kTolerance);
    if (!projection.IsDone() || projection.NbPoints() < 1)
        throw std::runtime_error("Continuity surface projection failed");
    double u, v;
    projection.LowerDistanceParameters(u, v);
    SurfaceJet031B jet;
    surface->D2(u, v, jet.point, jet.du, jet.dv, jet.duu, jet.dvv, jet.duv);
    jet.normal = jet.du.Crossed(jet.dv);
    if (jet.normal.Magnitude() <= 1e-14)
        throw std::runtime_error("Continuity is undefined at a singular surface checkpoint");
    jet.normal.Normalize();
    return jet;
}

// Second fundamental form expressed in one common world tangent basis.
// Comparing all three components checks curvature direction as well as magnitude.
std::array<double, 3> curvatureTensor031B(const SurfaceJet031B& jet, const gp_Vec& x, const gp_Vec& y, double sign) {
    const double e = jet.du.Dot(jet.du), f = jet.du.Dot(jet.dv), g = jet.dv.Dot(jet.dv);
    const double determinant = e * g - f * f;
    if (determinant <= 1e-24 * std::max(1., e * g))
        throw std::runtime_error("Curvature comparison has a singular surface metric");
    auto uv = [&](const gp_Vec& direction) {
        const double a = direction.Dot(jet.du), b = direction.Dot(jet.dv);
        return std::array<double, 2>{(g * a - f * b) / determinant, (e * b - f * a) / determinant};
    };
    const auto a = uv(x), b = uv(y);
    const double l = jet.duu.Dot(jet.normal) * sign, m = jet.duv.Dot(jet.normal) * sign, n = jet.dvv.Dot(jet.normal) * sign;
    auto bilinear = [&](const auto& p, const auto& q) { return l*p[0]*q[0] + m*(p[0]*q[1]+p[1]*q[0]) + n*p[1]*q[1]; };
    return {bilinear(a,a), bilinear(a,b), bilinear(b,b)};
}

// Preserve exact elementary patches when their authored four boundaries already
// determine a plane or a tensor-product Bezier strip. Continuity is still checked
// against every requested support below; these constructions cannot assert G2.
TopoDS_Shape elementaryPatch031B(const Profile& profile,std::string& method) {
    try {
        BRepBuilderAPI_MakeFace plane(profile.wire,true);
        if(plane.IsDone()&&BRepCheck_Analyzer(plane.Face()).IsValid()){method="exact_planar_boundary_face";return plane.Face();}
    }catch(const Standard_Failure&){}
    if(profile.edges.size()!=4)return {};
    for(size_t start=0;start<2;++start){
        const auto& a=profile.edges[start];const auto& b=profile.edges[start+2];
        const BRepAdaptor_Curve ca(a.edge),cb(b.edge);
        if(ca.GetType()!=GeomAbs_BezierCurve||cb.GetType()!=GeomAbs_BezierCurve||
           BRepAdaptor_Curve(profile.edges[(start+1)%4].edge).GetType()!=GeomAbs_Line||
           BRepAdaptor_Curve(profile.edges[(start+3)%4].edge).GetType()!=GeomAbs_Line)continue;
        const auto ba=ca.Bezier(),bb=cb.Bezier();const int count=ba->NbPoles();
        if(count!=bb->NbPoles()||std::abs(ca.FirstParameter())>1e-12||std::abs(ca.LastParameter()-1)>1e-12||std::abs(cb.FirstParameter())>1e-12||std::abs(cb.LastParameter()-1)>1e-12)continue;
        const bool reverseA=ba->StartPoint().Distance(a.first)>kTolerance,reverseB=bb->StartPoint().Distance(b.last)>kTolerance;
        NCollection_Array2<gp_Pnt> poles(1,count,1,2);NCollection_Array2<double> weights(1,count,1,2);
        for(int i=1;i<=count;++i){const int ia=reverseA?count-i+1:i,ib=reverseB?count-i+1:i;poles(i,1)=ba->Pole(ia);poles(i,2)=bb->Pole(ib);weights(i,1)=ba->Weight(ia);weights(i,2)=bb->Weight(ib);}
        Handle(Geom_BezierSurface) surface=new Geom_BezierSurface(poles,weights);
        BRepBuilderAPI_MakeFace face(surface,kTolerance);
        if(face.IsDone()&&BRepCheck_Analyzer(face.Face()).IsValid()){method="exact_tensor_product_bezier_boundary_strip";return face.Face();}
    }
    return {};
}

// A rectangular patch and one transverse, non-rational Bezier height guide have
// an exact tensor-product solution. A normalized Bernstein bubble interpolates
// the authored guide while vanishing, with its first (and for G2 second)
// derivatives, on the opposite borders. Every boundary/support/guide is still
// independently checked below. Other geometries use the native plate solver.
TopoDS_Shape guidedRectanglePatch059(const Profile& profile,const std::vector<Profile>& guides,int order,std::string& method) {
    if(profile.edges.size()!=4||guides.size()!=1||guides.front().edges.size()!=1)return {};
    for(const auto& edge:profile.edges)if(BRepAdaptor_Curve(edge.edge).GetType()!=GeomAbs_Line)return {};
    const auto a=profile.edges[0].first,b=profile.edges[0].last,d=profile.edges[3].first;
    const gp_Vec x(a,b),y(a,d);const double width=x.Magnitude(),height=y.Magnitude();
    if(width<=kTolerance||height<=kTolerance||std::abs(x.Dot(y))>1e-12*width*height||b.Translated(y).Distance(profile.edges[1].last)>1e-9)return {};
    const gp_Vec ux=x/width,uy=y/height,n=ux.Crossed(uy);
    const BRepAdaptor_Curve curve(guides.front().edges.front().edge);
    if(curve.GetType()!=GeomAbs_BezierCurve||std::abs(curve.FirstParameter())>1e-12||std::abs(curve.LastParameter()-1)>1e-12)return {};
    auto source=curve.Bezier();if(source->IsRational())return {};
    const int count=source->NbPoles();bool reversed=false;
    if(std::abs(gp_Vec(a,source->StartPoint()).Dot(ux)-width)<=1e-9&&std::abs(gp_Vec(a,source->EndPoint()).Dot(ux))<=1e-9)reversed=true;
    const auto pole=[&](int i){return source->Pole(reversed?count+1-i:i);};
    const double row=gp_Vec(a,pole(1)).Dot(uy)/height;if(row<=0||row>=1)return {};
    std::vector<double> offsets;
    for(int i=1;i<=count;++i){const gp_Vec delta(a,pole(i));if(std::abs(delta.Dot(ux)-width*(i-1)/(count-1.))>1e-9||std::abs(delta.Dot(uy)-height*row)>1e-9)return {};offsets.push_back(delta.Dot(n));}
    if(std::abs(offsets.front())>1e-12||std::abs(offsets.back())>1e-12)return {};
    const int degree=order==2?6:4,middle=degree/2;
    const double basis=(degree==6?20.:6.)*std::pow(row,middle)*std::pow(1-row,middle);
    if(!std::isfinite(basis)||basis<=1e-12)return {};
    NCollection_Array2<gp_Pnt> poles(1,count,1,degree+1);
    for(int i=1;i<=count;++i)for(int j=0;j<=degree;++j)poles(i,j+1)=a.Translated(x*((i-1)/(count-1.))+y*(j/double(degree))+n*(j==middle?offsets[i-1]/basis:0));
    Handle(Geom_BezierSurface) surface=new Geom_BezierSurface(poles);BRepBuilderAPI_MakeFace face(surface,kTolerance);
    if(!face.IsDone()||!BRepCheck_Analyzer(face.Face()).IsValid())throw std::runtime_error("Exact guided rectangle produced invalid topology");
    method="exact_tensor_product_bezier_interior_guide";return face.Face();
}

V constrainedSurfaceFill031B(const V& request) {
    auto profile = profileOf(required(request, "profile"), true);
    // Copy the complete boundary together: independent edge copies destroy the
    // shared vertex identities needed by OCCT's final WireFromList assembly.
    BRepBuilderAPI_Copy boundaryCopy(profile.wire,false,false);
    profile.wire=TopoDS::Wire(boundaryCopy.Shape());
    for(auto& edge:profile.edges)edge.edge=TopoDS::Edge(boundaryCopy.ModifiedShape(edge.edge));
    V constraints=V::makeArr();
    if(const auto* authored=request.find("constraints"))constraints=*authored;
    if (constraints.t != V::Arr || (constraints.arr.empty()&&!request.find("guide_curves")) || constraints.arr.size() > profile.edges.size())
        throw std::runtime_error("Constrained fill requires 1..edge_count support constraints");
    std::vector<Profile> guides;
    if(const auto* authored=request.find("guide_curves")) {
        if(authored->t!=V::Arr||authored->arr.empty()||authored->arr.size()>16)
            throw std::runtime_error("Guided patch requires 1..16 exact guide profiles");
        size_t edges=0;std::set<std::string> identities;
        for(const auto& value:authored->arr) {
            auto guide=profileOf(value,false);edges+=guide.edges.size();
            if(edges>128)throw std::runtime_error("Guided patch exceeds 128 guide edges");
            const auto identity=brepData(guide.wire);
            if(!identities.insert(identity).second)throw std::runtime_error("Guided patch contains a duplicate guide curve");
            BRepAlgoAPI_Check check(guide.wire,true,true);
            if(!check.IsValid())throw std::runtime_error("Patch guide failed native topology/self-interference checks");
            guides.push_back(std::move(guide));
        }
    }
    bool blend = false;
    if (const V* mode = request.find("mode")) {
        if (mode->t != V::Str || mode->str != "blend") throw std::runtime_error("Unknown constrained fill mode");
        blend = true;
    }
    int samples = 33;
    double tangentLimit = 0.001, curvatureLimit = 0.0001;
    if (const V* validation = request.find("validation")) {
        if (validation->t != V::Obj) throw std::runtime_error("Fill validation must be an object");
        for (const auto& field : validation->obj)
            if (field.first != "samples" && field.first != "tangent_deg" && field.first != "curvature_per_mm")
                throw std::runtime_error("Unknown fill validation field: " + field.first);
        if (const auto* v = validation->find("samples")) samples = integer(*v, "samples", 3, 129);
        if (const auto* v = validation->find("tangent_deg")) tangentLimit = number(*v, "tangent_deg");
        if (const auto* v = validation->find("curvature_per_mm")) curvatureLimit = number(*v, "curvature_per_mm");
    }
    if (tangentLimit <= 0 || tangentLimit > 0.1 || curvatureLimit <= 0 || curvatureLimit > 0.01)
        throw std::runtime_error("Fill validation limits require 0<tangent_deg<=0.1 and 0<curvature_per_mm<=0.01");
    const double tol = tolerance(request);
    std::map<size_t, std::pair<TopoDS_Face, int>> supports;
    std::set<std::string> distinctSupports;
    int order = 0;
    for (const auto& constraint : constraints.arr) {
        if (constraint.t != V::Obj) throw std::runtime_error("Fill constraint must be an object");
        for (const auto& field : constraint.obj)
            if (field.first != "edge" && field.first != "continuity" && field.first != "support_brep" && field.first != "topology" && field.first != "face")
                throw std::runtime_error("Unknown fill constraint field: " + field.first);
        const size_t edge = static_cast<size_t>(integer(required(constraint, "edge"), "edge", 0, static_cast<int>(profile.edges.size()-1)));
        const int continuity = integer(required(constraint, "continuity"), "continuity", 1, 2);
        if (!supports.emplace(edge, std::make_pair(supportFace031B(constraint), continuity)).second)
            throw std::runtime_error("A boundary edge may have only one support constraint");
        distinctSupports.insert(required(constraint,"support_brep").str + ":" + required(constraint,"face").str);
        order = std::max(order, continuity);
    }
    if (blend && (supports.size() < 2 || distinctSupports.size() < 2))
        throw std::runtime_error("Blend requires two distinct support faces and explicit connected boundaries");
    // Filling approximates the plate with a G0 criterion. Request a tighter
    // internal position fit so that the independent final D2 checks are useful;
    // public acceptance remains the fixed 1e-6 mm and authored angular/curvature
    // limits. No failed constraint is retried at a looser tolerance.
    const double constructionFit = 1e-8;
    BRepOffsetAPI_MakeFilling maker(std::max(3,order+2),33,3,false,constructionFit,constructionFit,
        tangentLimit * std::acos(-1.) / 180. * 0.1, curvatureLimit * 0.1, 12, 64);
    std::vector<TopoDS_Edge> edges;
    for (size_t i = 0; i < profile.edges.size(); ++i) {
        auto found = supports.find(i);
        if (found == supports.end()) {
            edges.push_back(profile.edges[i].edge);
            maker.Add(edges.back(), GeomAbs_C0, true);
        } else {
            edges.push_back(edgeOnSupport031B(profile.edges[i].edge, found->second.first, samples,true));
            // OCCT 8.0.1 forwards this enum unchanged to GeomPlate's integer
            // order (0/1/2), whose constructor rejects values above 2. In this
            // pinned SDK GeomAbs_G2 has ordinal 3. Pass the actual plate order,
            // and independently inspect D2 jets below rather than trusting labels.
            maker.Add(edges.back(), found->second.first, static_cast<GeomAbs_Shape>(found->second.second), true);
        }
    }
    for(const auto& guide:guides)for(const auto& edge:guide.edges)
        maker.Add(edge.edge,GeomAbs_C0,false); // interior exact-curve plate constraint
    std::string method;
    // An elementary boundary patch cannot silently override an interior guide.
    TopoDS_Shape patch=guides.empty()?elementaryPatch031B(profile,method):guidedRectanglePatch059(profile,guides,order,method);
    const bool elementary=!patch.IsNull();
    if(!elementary){
        maker.Build();
        if (!maker.IsDone()) throw std::runtime_error("OCCT support-constrained surface filling failed");
        if (!std::isfinite(maker.G0Error()) || maker.G0Error() > tol)
            throw std::runtime_error("Constrained fill exceeds fixed boundary position tolerance: "+std::to_string(maker.G0Error())+" mm");
        patch=maker.Shape();method=guides.empty()?"native_support_constrained_plate":"native_guided_support_constrained_plate";
    }
    const auto resultFaces = shapes(patch, TopAbs_FACE);
    if (resultFaces.size() != 1) throw std::runtime_error("Constrained fill must produce one valid patch");
    if(shapes(patch,TopAbs_EDGE).size()!=profile.edges.size())throw std::runtime_error("Constrained fill lost or added authored boundary edges");
    const auto filledSurface = BRep_Tool::Surface(TopoDS::Face(resultFaces.front()));
    double measuredBoundaryError=0;
    for(const auto& edge:profile.edges){BRepAdaptor_Curve curve(edge.edge);for(int i=0;i<samples;++i){const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/(samples-1.));BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(),patch);distance.Perform();if(!distance.IsDone()||!std::isfinite(distance.Value()))throw std::runtime_error("Patch boundary distance failed");measuredBoundaryError=std::max(measuredBoundaryError,distance.Value());}}
    if(measuredBoundaryError>tol)throw std::runtime_error("Patch misses an authored boundary at a native checkpoint");
    V guideEvidence=V::makeArr();double guideError=0;size_t guideSamples=0;
    for(size_t index=0;index<guides.size();++index) {
        double error=0;size_t checked=0;
        for(const auto& edge:guides[index].edges) {
            BRepAdaptor_Curve curve(edge.edge);
            for(int sample=0;sample<samples;++sample) {
                const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*sample/(samples-1.));
                BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(),patch);distance.Perform();
                if(!distance.IsDone()||!std::isfinite(distance.Value()))throw std::runtime_error("Guided patch native curve/face distance failed");
                error=std::max(error,distance.Value());++checked;
            }
        }
        if(error>tol)throw std::runtime_error("Guided patch misses guide "+std::to_string(index)+" or the guide leaves the retained patch: deviation_mm="+std::to_string(error));
        V row=V::makeObj();row.set("guide",V::makeNum(index));row.set("source_brep_sha256",V::makeStr(sha256(base64Decode(brepData(guides[index].wire)))));
        row.set("edges",V::makeNum(guides[index].edges.size()));row.set("samples_checked",V::makeNum(checked));row.set("max_curve_to_face_error_mm",V::makeNum(error));
        row.set("sampled_position_constraint_passed",V::makeBool(true));guideEvidence.arr.push_back(row);guideError=std::max(guideError,error);guideSamples+=checked;
    }
    V evidence = V::makeArr();
    for (const auto& entry : supports) {
        const auto edgeIndex = entry.first;
        const auto supportSurface = BRep_Tool::Surface(entry.second.first);
        BRepAdaptor_Curve curve(edges[edgeIndex]);
        double gap = 0, angle = 0, curvature = 0;
        for (int sample = 0; sample < samples; ++sample) {
            const double t = curve.FirstParameter() + (curve.LastParameter()-curve.FirstParameter()) * sample / (samples-1.);
            const gp_Pnt at = curve.Value(t);
            const auto a = jet031B(filledSurface, at), b = jet031B(supportSurface, at);
            gap = std::max({gap, a.point.Distance(b.point), a.point.Distance(at), b.point.Distance(at)});
            const double dot = a.normal.Dot(b.normal);
            angle = std::max(angle, std::acos(std::clamp(std::abs(dot), 0., 1.)) * 180. / std::acos(-1.));
            gp_Vec x = a.du.Normalized(), y = a.normal.Crossed(x);
            const auto tensorA = curvatureTensor031B(a, x, y, 1), tensorB = curvatureTensor031B(b, x, y, dot < 0 ? -1 : 1);
            // Frobenius norm bounds error in normal curvature for every tangent direction.
            const double aa = tensorA[0]-tensorB[0], ab = tensorA[1]-tensorB[1], bb = tensorA[2]-tensorB[2];
            curvature = std::max(curvature, std::sqrt(aa*aa + 2*ab*ab + bb*bb));
        }
        if (gap > tol || angle > tangentLimit || (entry.second.second == 2 && curvature > curvatureLimit))
            throw std::runtime_error("Constrained fill failed measured continuity on boundary edge " + std::to_string(edgeIndex) +
                ": gap_mm="+std::to_string(gap)+", tangent_deg="+std::to_string(angle)+", curvature_per_mm="+std::to_string(curvature));
        V row=V::makeObj();
        row.set("edge",V::makeNum(edgeIndex));row.set("requested_order",V::makeNum(entry.second.second));
        row.set("samples_checked",V::makeNum(samples));row.set("max_position_error_mm",V::makeNum(gap));
        row.set("max_tangent_plane_angle_deg",V::makeNum(angle));row.set("max_curvature_tensor_error_per_mm",V::makeNum(curvature));
        row.set("sampled_requested_continuity_passed",V::makeBool(true));evidence.arr.push_back(row);
    }
    V result=surfaceEditResult(patch,blend?"surface_blend":"surface_fill_constrained"),report=required(result,"report");
    report.set("boundary_continuity_requested",V::makeStr(order==2?"G2":order==1?"G1":"G0"));
    report.set("construction_method",V::makeStr(method));
    report.set("boundary_max_error_mm",V::makeNum(std::max(measuredBoundaryError,elementary?0:maker.G0Error())));
    report.set("boundary_samples_checked",V::makeNum(samples*profile.edges.size()));
    report.set("kernel_g1_error_rad",elementary?V{}:V::makeNum(maker.G1Error()));report.set("kernel_g2_error",elementary?V{}:V::makeNum(maker.G2Error()));
    report.set("support_boundary_evidence",evidence);report.set("position_tolerance_mm",V::makeNum(tol));
    report.set("internal_construction_fit_mm",elementary?V{}:V::makeNum(constructionFit));
    report.set("tangent_limit_deg",V::makeNum(tangentLimit));report.set("curvature_limit_per_mm",V::makeNum(curvatureLimit));
    report.set("continuity_scope",V::makeStr("Native D2 jets at uniform edge-parameter checkpoints including endpoints; unoriented tangent planes and common-basis second fundamental forms; between-sample extrema and continuous G1/G2 are unverified"));
    report.set("global_g1_verified",V::makeBool(false));report.set("global_g2_verified",V::makeBool(false));
    if(!guides.empty()) {
        report.set("guide_curve_evidence",guideEvidence);report.set("guide_samples_checked",V::makeNum(guideSamples));report.set("guide_max_error_mm",V::makeNum(guideError));
        report.set("guide_position_tolerance_mm",V::makeNum(tol));report.set("global_guides_verified",V::makeBool(false));
        report.set("guide_scope",V::makeStr("Interior native C0 curve constraints; finite-patch distance at uniform curve-parameter checkpoints including endpoints. No guide tangency, between-sample deviation bound or end-tangent loft claim."));
    }
    report.set("display_mesh_authoritative",V::makeBool(false));result.set("report",report);return result;
}

std::array<double,4> faceBounds031B(const TopoDS_Face& face) {
    std::array<double,4> b;BRepTools::UVBounds(face,b[0],b[1],b[2],b[3]);
    for(double v:b)if(!std::isfinite(v)||std::abs(v)>1e8)throw std::runtime_error("Surface edit requires finite UV bounds");
    return b;
}

V boundsValue031B(const std::array<double,4>& b) {
    V value=V::makeArr();for(double v:b)value.arr.push_back(V::makeNum(v));return value;
}

V untrimExtendSurface031B(const V& request, bool extend) {
    const auto face=singleSurface031B(required(request,"input_brep"),"Surface domain edit");
    const auto originalBounds=faceBounds031B(face);
    Handle(Geom_Surface) surface=BRep_Tool::Surface(face);
    // Removing the rectangular wrapper reveals its actual analytic/NURBS basis.
    while(!Handle(Geom_RectangularTrimmedSurface)::DownCast(surface).IsNull())
        surface=Handle(Geom_RectangularTrimmedSurface)::DownCast(surface)->BasisSurface();
    surface=Handle(Geom_Surface)::DownCast(surface->Copy());
    std::array<double,4> bounds;
    std::string method="explicit_uv_domain_on_unchanged_support";
    if(const V* extension=request.find("extension")) {
        if(!extend||request.find("bounds"))throw std::runtime_error("Length extension is exclusive to extend and cannot combine with bounds");
        if(extension->t!=V::Obj)throw std::runtime_error("extension must be an object");
        for(const auto& field:extension->obj)if(field.first!="direction"&&field.first!="length_mm"&&field.first!="continuity")throw std::runtime_error("Unknown extension field: "+field.first);
        const auto& direction=required(*extension,"direction");
        if(direction.t!=V::Str||(direction.str!="u_min"&&direction.str!="u_max"&&direction.str!="v_min"&&direction.str!="v_max"))throw std::runtime_error("Extension direction must be u_min, u_max, v_min or v_max");
        const double length=number(required(*extension,"length_mm"),"length_mm");
        if(length<=kTolerance||length>1e5)throw std::runtime_error("Extension length must be in (1e-6,100000] mm");
        const int continuity=integer(required(*extension,"continuity"),"continuity",1,3);
        auto bounded=Handle(Geom_BoundedSurface)::DownCast(surface);
        if(bounded.IsNull())throw std::runtime_error("Length extension requires a bounded surface; extend analytic supports with explicit bounds");
        // Restrict to the authored domain, preserving that region before extending.
        bounded=new Geom_RectangularTrimmedSurface(surface,originalBounds[0],originalBounds[1],originalBounds[2],originalBounds[3]);
        GeomLib::ExtendSurfByLength(bounded,length,continuity,direction.str[0]=='u',direction.str.substr(2)=="max");
        surface=bounded;surface->Bounds(bounds[0],bounds[1],bounds[2],bounds[3]);
        method="native_bounded_surface_length_extension";
    }else {
        const V& requested=required(request,"bounds");
        if(requested.t!=V::Arr||requested.arr.size()!=4)throw std::runtime_error("Untrim/extend requires explicit [u_min,u_max,v_min,v_max] bounds");
        for(size_t i=0;i<4;++i)bounds[i]=number(requested.arr[i],"UV bound");
        double u0,u1,v0,v1;surface->Bounds(u0,u1,v0,v1);
        if(bounds[0]<u0-1e-12||bounds[1]>u1+1e-12||bounds[2]<v0-1e-12||bounds[3]>v1+1e-12)
            throw std::runtime_error("Requested bounds exceed the natural support domain; use explicit length extension for bounded NURBS");
    }
    if(bounds[1]-bounds[0]<=1e-12||bounds[3]-bounds[2]<=1e-12)throw std::runtime_error("UV bounds must define a nondegenerate rectangle");
    if(bounds[0]>originalBounds[0]+1e-12||bounds[1]<originalBounds[1]-1e-12||bounds[2]>originalBounds[2]+1e-12||bounds[3]<originalBounds[3]-1e-12)
        throw std::runtime_error("Untrim/extend cannot discard any original UV region");
    if(surface->IsUPeriodic()&&bounds[1]-bounds[0]>surface->UPeriod()+1e-12)throw std::runtime_error("UV edit exceeds one U period");
    if(surface->IsVPeriodic()&&bounds[3]-bounds[2]>surface->VPeriod()+1e-12)throw std::runtime_error("UV edit exceeds one V period");
    BRepBuilderAPI_MakeFace maker(surface,bounds[0],bounds[1],bounds[2],bounds[3],kTolerance);
    if(!maker.IsDone())throw std::runtime_error("Native surface domain edit failed");
    TopoDS_Face edited=maker.Face();edited.Orientation(face.Orientation());
    const double before=properties(face,'a'),after=properties(edited,'a');
    if(!std::isfinite(after)||after-before<=1e-8)throw std::runtime_error("Untrim/extend must add a measured positive surface region");
    if(extend&&bounds==originalBounds)throw std::runtime_error("Extend requires an expanded UV domain");
    double preservedError=0;const auto oldSurface=BRep_Tool::Surface(face);
    for(int i=0;i<=8;++i)for(int j=0;j<=8;++j){const double u=originalBounds[0]+(originalBounds[1]-originalBounds[0])*i/8.,v=originalBounds[2]+(originalBounds[3]-originalBounds[2])*j/8.;preservedError=std::max(preservedError,oldSurface->Value(u,v).Distance(surface->Value(u,v)));}
    if(preservedError>kTolerance)throw std::runtime_error("Surface extension changed the original support beyond 1e-6 mm at a checkpoint");
    V result=surfaceEditResult(edited,extend?"surface_extend":"surface_untrim"),report=required(result,"report");
    report.set("original_uv_bounds",boundsValue031B(originalBounds));report.set("result_uv_bounds",boundsValue031B(bounds));
    report.set("original_area_mm2",V::makeNum(before));report.set("added_area_mm2",V::makeNum(after-before));
    report.set("original_support_samples",V::makeNum(81));report.set("original_support_max_error_mm",V::makeNum(preservedError));
    report.set("method",V::makeStr(method));report.set("holes_removed",V::makeBool(true));
    report.set("continuity_scope",V::makeStr("UV-domain replacement preserves the underlying support; length extension requests native C1/C2/C3, checks original support at 81 UV checkpoints, and does not certify global extension continuity"));
    result.set("report",report);return result;
}

V mutualTrimSurfaces031B(const V& request) {
    const auto a=singleSurface031B(required(request,"input_brep"),"Mutual trim first input"),b=singleSurface031B(required(request,"other_brep"),"Mutual trim second input");
    const auto& keep=required(request,"keep_points");
    if(keep.t!=V::Arr||keep.arr.size()!=2)throw std::runtime_error("Mutual trim requires two keep points, one per original surface");
    NCollection_List<TopoDS_Shape> args;args.Append(a);args.Append(b);
    BRepAlgoAPI_Splitter splitter;splitter.SetArguments(args);splitter.Build();
    if(!splitter.IsDone()||splitter.HasErrors())throw std::runtime_error("Exact mutual surface splitter failed");
    TopoDS_Compound retained;BRep_Builder builder;builder.MakeCompound(retained);
    V evidence=V::makeArr();
    for(int input=0;input<2;++input) {
        const TopoDS_Face& original=input==0?a:b;
        std::vector<TopoDS_Face> pieces;
        for(NCollection_List<TopoDS_Shape>::Iterator it(splitter.Modified(original));it.More();it.Next())
            for(const auto& f:shapes(it.Value(),TopAbs_FACE))if(std::none_of(pieces.begin(),pieces.end(),[&](const TopoDS_Face& p){return p.IsSame(f);}))pieces.push_back(TopoDS::Face(f));
        if(pieces.size()<2||pieces.size()>64)throw std::runtime_error("Mutual trim must divide both surfaces into 2..64 regions");
        const gp_Vec xyz=vector031B(keep.arr[input],"keep point");const gp_Pnt at(xyz.XYZ());
        TopoDS_Face selected;int matches=0;double total=0;
        for(const auto& piece:pieces) {
            total+=properties(piece,'a');BRepClass_FaceClassifier classify(piece,at,kTolerance);
            if(classify.State()==TopAbs_IN){BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(at).Vertex(),piece);distance.Perform();if(distance.IsDone()&&distance.Value()<=kTolerance){selected=piece;++matches;}}
        }
        if(matches!=1)throw std::runtime_error("Each mutual-trim keep point must lie inside one retained region and off the intersection");
        const double before=properties(original,'a'),after=properties(selected,'a');
        if(std::abs(total-before)>std::max(1e-6,before*1e-8)||before-after<=1e-8||after<=1e-8)
            throw std::runtime_error("Mutual trim failed per-input area conservation");
        builder.Add(retained,selected);
        V row=V::makeObj();row.set("input",V::makeNum(input));row.set("split_regions",V::makeNum(pieces.size()));
        row.set("original_area_mm2",V::makeNum(before));row.set("retained_area_mm2",V::makeNum(after));row.set("removed_area_mm2",V::makeNum(before-after));row.set("area_conservation_checked",V::makeBool(true));evidence.arr.push_back(row);
    }
    V result=shapeResult(retained,false),report=required(result,"report");
    report.set("operation",V::makeStr("surface_mutual_trim"));report.set("per_input",evidence);
    report.set("retained_faces",V::makeNum(2));report.set("sewn",V::makeBool(false));
    report.set("method",V::makeStr("exact_surface_surface_split_with_explicit_per_input_keep_point"));
    result.set("report",report);return result;
}
