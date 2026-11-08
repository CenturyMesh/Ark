// Explicit contour law. Existing straight-edge laws retain their073 semantics.
std::string filletFailure074(const BRepFilletAPI_MakeFillet& algorithm){
    std::ostringstream out;out<<"Native fillet failed within requested radii; no radius/tolerance fallback; faulty contours="<<algorithm.NbFaultyContours();
    for(int i=1;i<=algorithm.NbFaultyContours();++i){const int contour=algorithm.FaultyContour(i);out<<" ["<<contour<<": edges="<<algorithm.NbEdges(contour)<<", status="<<(int)algorithm.StripeStatus(contour)<<"]";}
    out<<"; faulty vertices="<<algorithm.NbFaultyVertices();for(int i=1;i<=algorithm.NbFaultyVertices();++i){const auto vertex=algorithm.FaultyVertex(i);if(!vertex.IsNull()){const auto p=BRep_Tool::Pnt(vertex);out<<" ("<<p.X()<<","<<p.Y()<<","<<p.Z()<<")";}}return out.str();
}
void variableContour074(BRepFilletAPI_MakeFillet& algorithm,const V& request,const std::vector<TopoDS_Edge>& edges,const std::vector<Ref>& refs,V& receipt){
    if(word(request,"radius_domain")!="contour")throw std::runtime_error("Variable fillet radius_domain must be contour");
    const auto& law=need(request,"radius_law");if(request.find("radius")||!request.find("edges"))throw std::runtime_error("Contour fillet requires radius_law and explicitly named edges only");
    if(law.t!=V::Arr||law.arr.size()<2||law.arr.size()>16)throw std::runtime_error("Contour radius_law needs 2..16 fraction/radius stations");NCollection_Array1<gp_Pnt2d> stations(1,(int)law.arr.size());double previous=-1;
    for(size_t i=0;i<law.arr.size();++i){const auto& row=law.arr[i];if(row.t!=V::Obj||row.obj.size()!=2||!row.find("fraction")||!row.find("radius"))throw std::runtime_error("Contour stations require fraction and radius");const double f=scalar(row,"fraction"),r=positive(row,"radius");if(f<0||f>1||f<=previous)throw std::runtime_error("Contour fractions must increase in [0,1]");stations.SetValue((int)i+1,gp_Pnt2d(f,r));previous=f;}
    if(stations.First().X()!=0||stations.Last().X()!=1)throw std::runtime_error("Contour law must include fractions 0 and 1");
    algorithm.Add(edges.front());if(algorithm.NbContours()!=1||algorithm.NbEdges(1)!=(int)edges.size()){
        std::string missing;for(int contour=1;contour<=algorithm.NbContours();++contour)for(int i=1;i<=algorithm.NbEdges(contour);++i){const auto& edge=algorithm.Edge(contour,i);bool selected=false;for(const auto& chosen:edges)selected|=chosen.IsSame(edge);if(selected)continue;std::string label;for(const auto& ref:refs)if(ref.kind=="edge"&&selectable(ref.status)&&ref.shape.IsSame(edge)){if(label.empty()||ref.name.find("@ark.")==std::string::npos)label=ref.name;if(ref.name.find("@ark.")==std::string::npos)break;}if(label.empty())label="native contour edge "+std::to_string(i);missing+=(missing.empty()?"":"; ")+label;}
        throw std::runtime_error("Select every edge of exactly one tangent contour; no implicit edge is added"+(missing.empty()?std::string("; selected edges are repeated or outside this contour"):"; missing: "+missing));
    }
    for(int i=1;i<=algorithm.NbEdges(1);++i){int matches=0;for(const auto& edge:edges)matches+=edge.IsSame(algorithm.Edge(1,i));if(matches!=1)throw std::runtime_error("Contour selection repeats an edge or omits a tangent edge");}
    const auto first=algorithm.FirstVertex(1),last=algorithm.LastVertex(1);if(first.IsNull()||last.IsNull())throw std::runtime_error("Contour law has no verifiable endpoint origin");const bool closed=algorithm.Closed(1);
    BRepAdaptor_Curve curve(edges.front());gp_Pnt midpoint;gp_Vec derivative;curve.D1((curve.FirstParameter()+curve.LastParameter())*.5,midpoint,derivative);if(derivative.Magnitude()<=1e-12)throw std::runtime_error("Contour anchor has an undefined tangent");
    const auto& selected=need(request,"edges").arr.front().str;bool found=false,semanticReverse=false;for(const auto& ref:refs)if(ref.name==selected){const auto d=descriptor(ref);const auto v=point(need(d,"tangent"),"semantic tangent");semanticReverse=derivative.Dot(gp_Vec(v.X(),v.Y(),v.Z()))<0;found=true;break;}if(!found)throw std::runtime_error("Contour anchor reference is missing");
    TopoDS_Vertex edgeFirst,edgeLast;TopExp::Vertices(TopoDS::Edge(edges.front().Oriented(TopAbs_FORWARD)),edgeFirst,edgeLast);if(edgeFirst.IsNull()||edgeLast.IsNull())throw std::runtime_error("Contour anchor edge has no vertices");if(semanticReverse)std::swap(edgeFirst,edgeLast);
    bool reversed=false;TopoDS_Vertex origin;
    if(closed){
        if(stations.First().Y()!=stations.Last().Y())throw std::runtime_error("Closed contour radii at fractions 0 and 1 must be equal");
        if(!edgeFirst.IsSame(first))throw std::runtime_error("Closed contour origin must equal the directed start of the first named edge");origin=edgeFirst;
        const auto contourEdge=algorithm.Edge(1,1);TopoDS_Vertex a,b;TopExp::Vertices(contourEdge,a,b,true);if(a.IsNull()||!a.IsSame(origin)||!contourEdge.IsSame(edges.front()))throw std::runtime_error("Closed contour traversal has no exact authored anchor");
        // For a periodic single edge, vertices coincide. Its oriented curve
        // derivative is the remaining exact direction witness.
        reversed=(contourEdge.Orientation()==TopAbs_REVERSED)!=semanticReverse;
    }else if(edges.size()==1){
        origin=edgeFirst;if(origin.IsSame(first))reversed=false;else if(origin.IsSame(last))reversed=true;else throw std::runtime_error("Directed edge origin does not match a contour endpoint");
    }else{
        const bool touchesFirst=edgeFirst.IsSame(first)||edgeLast.IsSame(first),touchesLast=edgeFirst.IsSame(last)||edgeLast.IsSame(last);if(touchesFirst==touchesLast)throw std::runtime_error("First named edge must touch exactly one endpoint of the tangent chain");reversed=touchesLast;origin=reversed?last:first;
    }
    if(reversed){NCollection_Array1<gp_Pnt2d> reverse(1,stations.Size());for(int i=1;i<=stations.Size();++i){const auto q=stations.Value(stations.Size()+1-i);reverse.SetValue(i,gp_Pnt2d(1-q.X(),q.Y()));}stations=reverse;}
    const double length=algorithm.Length(1);if(!std::isfinite(length)||length<=1e-7)throw std::runtime_error("Contour length is degenerate");
    // OCCT's public array overload normalizes its stations on ONE edge,
    // despite its contour wording. Distribute global stations explicitly;
    // boundary radii are linear interpolants of the authored neighboring
    // stations, so every chain edge receives a complete continuous law.
    auto radiusAt=[&](double f){for(int i=2;i<=stations.Size();++i)if(f<=stations.Value(i).X()){const auto a=stations.Value(i-1),b=stations.Value(i);return a.Y()+(b.Y()-a.Y())*(f-a.X())/(b.X()-a.X());}return stations.Last().Y();};
    double traversed=0;for(int e=1;e<=algorithm.NbEdges(1);++e){GProp_GProps property;BRepGProp::LinearProperties(algorithm.Edge(1,e),property);const double span=property.Mass();if(!std::isfinite(span)||span<=1e-7)throw std::runtime_error("Contour has a degenerate edge length");const double a=traversed/length;traversed+=span;const double b=e==algorithm.NbEdges(1)?1.:traversed/length;if(b<=a||b>1.+1e-8)throw std::runtime_error("Contour edge lengths disagree with the native spine");std::vector<gp_Pnt2d> points{gp_Pnt2d(0,radiusAt(a))};for(int i=2;i<stations.Size();++i){const auto p=stations.Value(i);if(p.X()>a&&p.X()<b)points.emplace_back((p.X()-a)/(b-a),p.Y());}points.emplace_back(1,radiusAt(b));NCollection_Array1<gp_Pnt2d> local(1,(int)points.size());for(size_t i=0;i<points.size();++i)local.SetValue((int)i+1,points[i]);algorithm.SetRadius(local,1,e);}
    if(std::abs(traversed-length)>std::max(1e-7,length*1e-9))throw std::runtime_error("Contour cumulative edge length differs from native spine length");
    receipt.set("mode",V::makeStr("variable_radius"));receipt.set("radius_domain",V::makeStr("contour"));receipt.set("radius_law",law);receipt.set("fraction_parameter",V::makeStr("normalized_contour_arclength"));receipt.set("interpolation",V::makeStr("native_per_edge_with_linear_boundary_samples"));receipt.set("anchor_edge",V::makeStr(selected));receipt.set("origin_mm",xyz(BRep_Tool::Pnt(origin)));receipt.set("contour_length_mm",V::makeNum(length));receipt.set("closed_contour",V::makeBool(closed));receipt.set("law_reversed_to_native_contour",V::makeBool(reversed));receipt.set("implicit_edges_added",V::makeNum(0));
}
