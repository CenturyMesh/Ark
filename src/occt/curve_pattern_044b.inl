// 044B curve patterns. One continuous arc-length parametrization over an
// auto-chained edge list, a whole wire path or an exact cable centerline;
// count or pitch spacing, closed loops without a seam duplicate, translate,
// x_hint, frenet or support-surface frames. Copies are located shapes that
// share the validated seed TShape. A request with only the legacy fields
// {reference,count,margin_mm,offset,x_hint} keeps the original code path.
namespace {
bool curvePattern044b(const V& request){
    static const std::set<std::string> fields{"references","pitch_mm","pitch_mode","start_margin_mm","end_margin_mm","phase_mm","skip","reverse","orientation","support_faces","project","offset_mm","max_deviation_mm","flip_normal","reference_scheme","named_copies","support_brep","support_topology","tool_centerline"};
    if(!request.find("reference"))return true;for(const auto& field:request.obj)if(fields.count(field.first))return true;return false;
}
struct PatternEdge044b{TopoDS_Edge edge;Handle(BRepAdaptor_Curve) curve;bool reversed=false;double length=0,start=0;};
struct PatternPath044b{std::vector<PatternEdge044b> edges;double length=0,gap=0;bool closed=false;};
struct PatternStation044b{gp_Pnt point;gp_Vec tangent,normal;bool curved=false;double s=0;};
bool flag044b(const V& request,const char* key){const V* value=request.find(key);if(!value)return false;if(value->t!=V::Bool)throw std::runtime_error(std::string("Curve pattern ")+key+" must be boolean");return value->b;}
TopoDS_Edge centerlineEdge044b(const V& c){
    if(c.t!=V::Obj||word(c,"kind")!="nonrational_bspline")throw std::runtime_error("Curve pattern cable centerline must be a nonrational B-spline");
    const double degree=scalar(c,"degree");const auto& poles=need(c,"poles_mm"),&knots=need(c,"knots"),&mults=need(c,"multiplicities");
    if(degree<1||degree>9||degree!=std::floor(degree)||poles.t!=V::Arr||knots.t!=V::Arr||mults.t!=V::Arr||poles.arr.size()<2||poles.arr.size()>1024||knots.arr.size()<2||knots.arr.size()!=mults.arr.size())throw std::runtime_error("Invalid cable centerline B-spline");
    NCollection_Array1<gp_Pnt> p(1,(int)poles.arr.size());for(size_t i=0;i<poles.arr.size();++i)p((int)i+1)=point(poles.arr[i],"centerline pole");
    NCollection_Array1<double> k(1,(int)knots.arr.size());NCollection_Array1<int> m(1,(int)knots.arr.size());
    for(size_t i=0;i<knots.arr.size();++i){if(knots.arr[i].t!=V::Num||!std::isfinite(knots.arr[i].num)||mults.arr[i].t!=V::Num||mults.arr[i].num<1||mults.arr[i].num!=std::floor(mults.arr[i].num))throw std::runtime_error("Invalid cable centerline knot vector");k((int)i+1)=knots.arr[i].num;m((int)i+1)=(int)mults.arr[i].num;}
    Handle(Geom_BSplineCurve) curve=new Geom_BSplineCurve(p,k,m,(int)degree);BRepBuilderAPI_MakeEdge edge(curve);if(!edge.IsDone())throw std::runtime_error("Cable centerline edge failed");return edge.Edge();
}
std::vector<TopoDS_Edge> patternPathEdges044b(const V& request,std::string& source){
    if(const V* centerline=request.find("tool_centerline")){if(request.find("reference")||request.find("references"))throw std::runtime_error("A cable centerline path takes no edge references");source="cable_centerline";return {centerlineEdge044b(*centerline)};}
    const TopoDS_Shape path=readBrep(word(request,"tool_brep"));std::vector<std::string> names;
    if(request.find("reference")){if(request.find("references"))throw std::runtime_error("Use reference or references, not both");names.push_back(word(request,"reference"));}
    else if(const V* list=request.find("references")){if(list->t!=V::Arr||list->arr.empty()||list->arr.size()>512)throw std::runtime_error("Curve pattern references need 1..512 edge names");for(const auto& item:list->arr){if(item.t!=V::Str||item.str.empty())throw std::runtime_error("Curve pattern references must be edge names");names.push_back(item.str);}}
    std::vector<TopoDS_Edge> edges;
    if(names.empty()){
        if(!shapes(path,TopAbs_FACE).empty())throw std::runtime_error("A whole-feature curve pattern path must be a wire-only curve feature; this path has faces, so select edge references");
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> unique;TopExp::MapShapes(path,TopAbs_EDGE,unique);for(int i=1;i<=unique.Extent();++i){const auto e=TopoDS::Edge(unique(i));if(!BRep_Tool::Degenerated(e))edges.push_back(e);}
        if(edges.empty())throw std::runtime_error("Curve pattern path feature has no edges");if(edges.size()>512)throw std::runtime_error("Curve pattern path has more than 512 edges");source="feature_wire";return edges;
    }
    const auto refs=inputRefs(request,"tool_brep","tool_topology",path);std::set<std::string> seen;
    for(const auto& name:names){
        if(!seen.insert(name).second)throw std::runtime_error("Repeated curve pattern reference: "+name);const Ref* found=nullptr;for(const auto& ref:refs)if(ref.name==name)found=&ref;
        if(!found||!selectable(found->status)||found->shape.IsNull())throw std::runtime_error("Missing, split or deleted curve pattern reference: "+name);if(found->kind!="edge")throw std::runtime_error("Curve pattern reference is not an edge: "+name);
        const auto e=TopoDS::Edge(found->shape);if(BRep_Tool::Degenerated(e))throw std::runtime_error("Curve pattern reference is a degenerated edge: "+name);for(const auto& prior:edges)if(prior.IsSame(e))throw std::runtime_error("Curve pattern references name one edge twice: "+name);edges.push_back(e);
    }
    source=names.size()==1?"reference":"references";return edges;
}
// Edge ends join when they share a vertex or lie within the sum of their own
// vertex tolerances; no larger tolerance is introduced. The first listed edge
// fixes the travel direction and, on a closed loop, the seam (station 0).
PatternPath044b chainPatternPath044b(const std::vector<TopoDS_Edge>& input,bool reverse){
    const size_t n=input.size();if(n<1||n>512)throw std::runtime_error("Curve pattern path needs 1..512 edges");
    struct End{gp_Pnt point;TopoDS_Vertex vertex;double tolerance=0;};std::vector<PatternEdge044b> edges(n);std::vector<End> ends(2*n);
    for(size_t i=0;i<n;++i){auto& e=edges[i];e.edge=input[i];e.curve=new BRepAdaptor_Curve(e.edge);const double a=e.curve->FirstParameter(),b=e.curve->LastParameter();
        if(!std::isfinite(a)||!std::isfinite(b)||!(b>a))throw std::runtime_error("Curve pattern path edge "+std::to_string(i)+" has an unbounded or empty parameter range");
        e.length=GCPnts_AbscissaPoint::Length(*e.curve,1e-10);if(!std::isfinite(e.length)||e.length<=1e-9)throw std::runtime_error("Curve pattern path edge "+std::to_string(i)+" has zero length");
        TopoDS_Vertex first,last;TopExp::Vertices(e.edge,first,last,false);ends[2*i]={e.curve->Value(a),first,first.IsNull()?0.:BRep_Tool::Tolerance(first)};ends[2*i+1]={e.curve->Value(b),last,last.IsNull()?0.:BRep_Tool::Tolerance(last)};}
    std::vector<size_t> parent(2*n);for(size_t i=0;i<parent.size();++i)parent[i]=i;auto root=[&](size_t x){while(parent[x]!=x)x=parent[x]=parent[parent[x]];return x;};double gap=0;
    for(size_t a=0;a<2*n;++a)for(size_t b=a+1;b<2*n;++b){const bool sameVertex=!ends[a].vertex.IsNull()&&ends[a].vertex.IsSame(ends[b].vertex);const double d=ends[a].point.Distance(ends[b].point);
        const bool join=a/2==b/2?(sameVertex||edges[a/2].curve->IsClosed()):(sameVertex||d<=std::max(1e-7,ends[a].tolerance+ends[b].tolerance));if(join){parent[root(a)]=root(b);gap=std::max(gap,d);}}
    std::map<size_t,std::vector<size_t>> clusters;for(size_t x=0;x<2*n;++x)clusters[root(x)].push_back(x);
    for(const auto& cluster:clusters)if(cluster.second.size()>2)throw std::runtime_error("Curve pattern path branches: "+std::to_string(cluster.second.size())+" edge ends meet at one point; select one simple chain");
    auto other=[&](size_t end)->long long{for(size_t x:clusters[root(end)])if(x!=end)return (long long)x;return -1;};
    size_t edge=0;int in=0;
    for(size_t step=0;step<=n;++step){const long long previous=other(2*edge+(size_t)in);if(previous<0)break;edge=(size_t)previous/2;in=1-(int)(previous%2);if(edge==0&&in==0)break;}
    std::vector<PatternEdge044b> ordered;std::vector<bool> used(n,false);const size_t startEdge=edge;const int startIn=in;bool closed=false;
    for(;;){if(used[edge])throw std::runtime_error("Curve pattern path revisits an edge; select one simple chain");used[edge]=true;auto item=edges[edge];item.reversed=in==1;ordered.push_back(item);
        const long long next=other(2*edge+(size_t)(1-in));if(next<0)break;const size_t f=(size_t)next/2;const int k=(int)(next%2);if(f==startEdge&&k==startIn){closed=true;break;}edge=f;in=k;}
    if(ordered.size()!=n)throw std::runtime_error("Curve pattern path is disconnected: "+std::to_string(n-ordered.size())+" of "+std::to_string(n)+" edges do not join the chain through the first edge within their vertex tolerances");
    if(reverse){std::reverse(ordered.begin(),ordered.end());for(auto& e:ordered)e.reversed=!e.reversed;}
    PatternPath044b path;double s=0;for(auto& e:ordered){e.start=s;s+=e.length;}path.edges=std::move(ordered);path.length=s;path.closed=closed;path.gap=gap;return path;
}
PatternStation044b patternStation044b(const PatternPath044b& path,double s){
    if(path.closed){s=std::fmod(s,path.length);if(s<0)s+=path.length;}
    auto it=std::upper_bound(path.edges.begin(),path.edges.end(),s,[](double value,const PatternEdge044b& e){return value<e.start;});const auto& e=path.edges[it==path.edges.begin()?0:(size_t)(it-path.edges.begin())-1];
    const double d=std::clamp(s-e.start,0.,e.length),a=e.reversed?e.length-d:d;const auto& c=*e.curve;double u=a<=0?c.FirstParameter():c.LastParameter();
    if(a>0&&a<e.length){GCPnts_AbscissaPoint ab(c,a,c.FirstParameter(),1e-8);if(!ab.IsDone())throw std::runtime_error("Curve pattern arc length inversion failed");u=ab.Parameter();
        // Newton on the tolerance-driven length: the inversion alone drifts ~1e-4 mm on multi-span splines.
        for(int k=0;k<6;++k){const double error=a-GCPnts_AbscissaPoint::Length(c,c.FirstParameter(),u,1e-10);gp_Pnt p;gp_Vec d;c.D1(u,p,d);if(std::abs(error)<1e-10||d.Magnitude()<1e-12)break;u=std::clamp(u+error/d.Magnitude(),c.FirstParameter(),c.LastParameter());}}
    BRepLProp_CLProps prop(c,u,2,1e-9);if(!prop.IsTangentDefined())throw std::runtime_error("Curve pattern tangent is undefined at arc length "+std::to_string(s)+" mm");
    gp_Dir t;prop.Tangent(t);PatternStation044b out;out.point=prop.Value();out.tangent=gp_Vec(t);if(e.reversed)out.tangent.Reverse();out.s=s;
    if(prop.Curvature()>1e-9){gp_Dir normal;prop.Normal(normal);out.normal=gp_Vec(normal);out.curved=true;}return out;
}
gp_Vec supportNormal044b(const TopoDS_Face& face,double u,double v){BRepAdaptor_Surface surface(face);BRepLProp_SLProps props(surface,u,v,1,1e-9);if(!props.IsNormalDefined())throw std::runtime_error("Curve pattern support normal is undefined");gp_Vec n(props.Normal());if(face.Orientation()==TopAbs_REVERSED)n.Reverse();return n;}
TopoDS_Shape curvePatternNative044b(const TopoDS_Shape& input,const V& request,std::vector<Ref>& refs,V& receipt){
    try{
    static const std::set<std::string> allowed{"op","reference","references","count","pitch_mm","pitch_mode","margin_mm","start_margin_mm","end_margin_mm","phase_mm","skip","reverse","offset","x_hint","orientation","support_faces","project","offset_mm","max_deviation_mm","flip_normal","reference_scheme","named_copies","input_brep","input_topology","tool_brep","tool_topology","tool_namespace","tool_centerline","support_brep","support_topology"};
    for(const auto& field:request.obj)if(!allowed.count(field.first))throw std::runtime_error("Unknown curve pattern field: "+field.first);
    std::string source;const auto path=chainPatternPath044b(patternPathEdges044b(request,source),flag044b(request,"reverse"));
    const bool hasCount=request.find("count")!=nullptr,hasPitch=request.find("pitch_mm")!=nullptr;if(hasCount==hasPitch)throw std::runtime_error("Curve pattern needs exactly one of count or pitch_mm");
    if(!hasPitch&&request.find("pitch_mode"))throw std::runtime_error("pitch_mode requires pitch_mm");const std::string pitchMode=request.find("pitch_mode")?word(request,"pitch_mode"):"fit";if(pitchMode!="fit"&&pitchMode!="truncate")throw std::runtime_error("pitch_mode must be fit or truncate");
    const double margin=optional(request,"margin_mm",0),start=optional(request,"start_margin_mm",margin),end=optional(request,"end_margin_mm",margin);if(margin<0||start<0||end<0)throw std::runtime_error("Curve pattern margins must be nonnegative");
    const bool loop=path.closed&&start==0&&end==0;if(!loop&&request.find("phase_mm"))throw std::runtime_error("phase_mm applies only to a closed path without margins");
    const double phase=optional(request,"phase_mm",0);if(loop&&(phase<0||phase>=path.length))throw std::runtime_error("phase_mm must be in [0, closed path length)");
    const double usable=loop?path.length:path.length-start-end;if(!(usable>1e-9))throw std::runtime_error("Curve pattern margins leave no usable path length: path "+std::to_string(path.length)+" mm");
    size_t n=0;double pitch=0,remainder=0;std::string spacing;
    if(hasCount){const double count=scalar(request,"count");if(count<2||count>2048||count!=std::floor(count))throw std::runtime_error("Curve pattern count must be 2..2048");n=(size_t)count;pitch=usable/(double)(loop?n:n-1);spacing="count";}
    else{const double requested=positive(request,"pitch_mm"),intervals=usable/requested;if(intervals>4096)throw std::runtime_error("pitch_mm yields more than 2048 copies");
        const double k=pitchMode=="fit"?std::max(1.,std::round(intervals)):std::floor(intervals+1e-9);n=(size_t)k+(loop?0:1);pitch=pitchMode=="fit"?usable/k:requested;remainder=pitchMode=="fit"?0:usable-k*requested;spacing=pitchMode=="fit"?"pitch_fit":"pitch_truncate";
        if(n<2||n>2048)throw std::runtime_error("pitch_mm yields "+std::to_string(n)+" copies; curve pattern needs 2..2048");}
    std::vector<double> abscissa(n);for(size_t i=0;i<n;++i)abscissa[i]=loop?phase+(spacing=="pitch_truncate"?pitch*(double)i:path.length*(double)i/(double)n):start+(spacing=="pitch_truncate"?pitch*(double)i:usable*(double)i/(double)(n-1));
    std::set<size_t> skipped;if(const V* skip=request.find("skip")){if(skip->t!=V::Arr||skip->arr.size()>2048)throw std::runtime_error("Curve pattern skip must be an array of station indices");for(const auto& item:skip->arr){if(item.t!=V::Num||item.num<0||item.num!=std::floor(item.num)||item.num>=(double)n)throw std::runtime_error("Curve pattern skip index outside 0..stations-1");if(!skipped.insert((size_t)item.num).second)throw std::runtime_error("Repeated curve pattern skip index");}}
    std::vector<size_t> kept;for(size_t i=0;i<n;++i)if(!skipped.count(i))kept.push_back(i);if(kept.empty())throw std::runtime_error("Curve pattern skips every station");
    const std::string orientation=request.find("orientation")?word(request,"orientation"):request.find("x_hint")?"x_hint":"translate";
    if(orientation!="translate"&&orientation!="x_hint"&&orientation!="frenet"&&orientation!="surface")throw std::runtime_error("Curve pattern orientation must be translate, x_hint, frenet or surface");
    if(orientation=="x_hint"&&!request.find("x_hint"))throw std::runtime_error("x_hint orientation needs x_hint");if((orientation=="translate"||orientation=="surface")&&request.find("x_hint"))throw std::runtime_error(orientation+" orientation does not use x_hint; remove it");
    const bool surface=orientation=="surface",project=flag044b(request,"project"),flip=flag044b(request,"flip_normal");
    for(const char* key:{"support_faces","project","offset_mm","max_deviation_mm","flip_normal"})if(!surface&&request.find(key))throw std::runtime_error(std::string(key)+" requires orientation surface");
    if(request.find("offset_mm")&&!project)throw std::runtime_error("offset_mm requires project:true");const double offsetMm=optional(request,"offset_mm",0);if(std::abs(offsetMm)>1e4)throw std::runtime_error("offset_mm exceeds 10000 mm");
    gp_Vec hint;if(const V* value=request.find("x_hint")){hint=gp_Vec(point(*value,"x_hint").XYZ());if(hint.Magnitude()<1e-12)throw std::runtime_error("Pattern x_hint is zero");}
    gp_Vec shift(0,0,0);if(const auto* value=request.find("offset"))shift=gp_Vec(point(*value,"offset").XYZ());
    // Seed census and explicit bounded capacities, checked before any copy.
    const std::string scheme=request.find("reference_scheme")?word(request,"reference_scheme"):"copies";if(scheme!="copies"&&scheme!="compact")throw std::runtime_error("reference_scheme must be copies or compact");
    std::vector<Ref> seedRefs;for(const auto& ref:refs)if(ref.name.find("@ark.")==std::string::npos)seedRefs.push_back(ref);
    std::vector<size_t> named;if(scheme=="compact"){named={kept.front()};if(const V* list=request.find("named_copies")){named.clear();if(list->t!=V::Arr||list->arr.size()>2048)throw std::runtime_error("named_copies must be an array of station indices");for(const auto& item:list->arr){if(item.t!=V::Num||item.num<0||item.num!=std::floor(item.num)||item.num>=(double)n||skipped.count((size_t)item.num))throw std::runtime_error("named_copies must list kept station indices");if(std::find(named.begin(),named.end(),(size_t)item.num)!=named.end())throw std::runtime_error("Repeated named copy");named.push_back((size_t)item.num);}}}
    else if(request.find("named_copies"))throw std::runtime_error("named_copies requires reference_scheme compact");
    const size_t semantic=(scheme=="copies"?kept.size():named.size())*seedRefs.size();
    if(semantic>512)throw std::runtime_error("Curve pattern would publish "+std::to_string(semantic)+" semantic references (limit 512): use reference_scheme:\"compact\" with named_copies, or reduce copies to "+std::to_string(seedRefs.empty()?0:512/seedRefs.size()));
    const auto seedFaces=shapes(input,TopAbs_FACE),seedEdges=shapes(input,TopAbs_EDGE);NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> uniqueEdges;size_t selectableEdges=0;for(const auto& e:seedEdges){const int before=uniqueEdges.Extent();uniqueEdges.Add(e);if(uniqueEdges.Extent()>before&&!BRep_Tool::Degenerated(TopoDS::Edge(e)))++selectableEdges;}
    const size_t perCopy=seedFaces.size()+selectableEdges,inventory=kept.size()*perCopy+semantic;
    if(inventory>16384)throw std::runtime_error("Curve pattern selection inventory would need "+std::to_string(inventory)+" references (capacity 16384): reduce copies to "+std::to_string(perCopy?(16384-std::min<size_t>(semantic,16384))/perCopy:0)+" or simplify the seed ("+std::to_string(perCopy)+" faces/edges per copy)");
    std::vector<PatternStation044b> stations;stations.reserve(kept.size());for(size_t i:kept){auto station=patternStation044b(path,abscissa[i]);station.point.Translate(shift);stations.push_back(station);}
    std::vector<gp_Vec> xs(stations.size());std::vector<gp_Pnt> origins(stations.size());std::vector<gp_Vec> zs(stations.size());size_t carried=0;V surfaceReceipt=V::makeObj();
    if(orientation=="frenet"){
        size_t first=stations.size();for(size_t i=0;i<stations.size();++i)if(stations[i].curved){first=i;break;}
        if(first==stations.size()&&hint.Magnitude()<1e-12)throw std::runtime_error("Frenet frame is undefined on a path without curvature at the stations; give x_hint or use orientation x_hint");
        gp_Vec previous=first<stations.size()?stations[first].normal:hint;
        for(size_t i=0;i<stations.size();++i){gp_Vec z=stations[i].tangent,x=stations[i].curved?stations[i].normal:previous;if(!stations[i].curved){x-=z*x.Dot(z);++carried;if(x.Magnitude()<1e-9)throw std::runtime_error("Frenet frame cannot carry its normal through station "+std::to_string(kept[i]));}x.Normalize();xs[i]=x;zs[i]=z;origins[i]=stations[i].point;previous=x;}
    }else if(surface){
        const bool own=request.find("support_brep")!=nullptr;if(!own&&request.find("tool_centerline"))throw std::runtime_error("Surface orientation on a cable centerline needs a third support input");
        const char* brepKey=own?"support_brep":"tool_brep";const char* topologyKey=own?"support_topology":"tool_topology";const TopoDS_Shape supportShape=readBrep(word(request,brepKey));std::vector<TopoDS_Shape> faces;
        if(const V* names=request.find("support_faces")){
            if(names->t!=V::Arr||names->arr.empty()||names->arr.size()>256)throw std::runtime_error("support_faces needs 1..256 face names");const auto supportRefs=inputRefs(request,brepKey,topologyKey,supportShape);
            for(const auto& item:names->arr){if(item.t!=V::Str)throw std::runtime_error("support_faces must be face names");const Ref* found=nullptr;for(const auto& ref:supportRefs)if(ref.name==item.str)found=&ref;if(!found||!selectable(found->status)||found->shape.IsNull()||found->kind!="face")throw std::runtime_error("Missing or non-face support reference: "+item.str);for(const auto& prior:faces)if(prior.IsSame(found->shape))throw std::runtime_error("Repeated support face: "+item.str);faces.push_back(found->shape);}
        }else{NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> unique;TopExp::MapShapes(supportShape,TopAbs_FACE,unique);for(int i=1;i<=unique.Extent();++i)faces.push_back(unique(i));}
        if(faces.empty())throw std::runtime_error("Surface orientation needs a support body with faces");
        const TopoDS_Shape target=compound(faces);NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> edgeFaces,vertexFaces;TopExp::MapShapesAndAncestors(target,TopAbs_EDGE,TopAbs_FACE,edgeFaces);TopExp::MapShapesAndAncestors(target,TopAbs_VERTEX,TopAbs_FACE,vertexFaces);
        BRepExtrema_DistShapeShape nearest;nearest.LoadS2(target);double worst=0,total=0;size_t worstStation=kept.front(),boundary=0;const double limit=request.find("max_deviation_mm")?positive(request,"max_deviation_mm"):-1;
        for(size_t i=0;i<stations.size();++i){
            nearest.LoadS1(BRepBuilderAPI_MakeVertex(stations[i].point).Vertex());if(!nearest.Perform()||!nearest.IsDone()||nearest.NbSolution()<1)throw std::runtime_error("Support nearest-point search failed at station "+std::to_string(kept[i]));
            const gp_Pnt q=nearest.PointOnShape2(1);const double deviation=nearest.Value();gp_Vec n(0,0,0);const auto type=nearest.SupportTypeShape2(1);const TopoDS_Shape support=nearest.SupportOnShape2(1);
            if(type==BRepExtrema_IsInFace){double u=0,v=0;nearest.ParOnFaceS2(1,u,v);n=supportNormal044b(TopoDS::Face(support),u,v);}
            else{++boundary;const auto& owners=type==BRepExtrema_IsOnEdge?edgeFaces:vertexFaces;if(!owners.Contains(support))throw std::runtime_error("Support boundary has no owning face at station "+std::to_string(kept[i]));
                for(NCollection_List<TopoDS_Shape>::Iterator it(owners.FindFromKey(support));it.More();it.Next()){const auto face=TopoDS::Face(it.Value());gp_Pnt2d uv;
                    if(type==BRepExtrema_IsOnEdge){double t=0,f=0,l=0;nearest.ParOnEdgeS2(1,t);const auto curve=BRep_Tool::CurveOnSurface(TopoDS::Edge(support),face,f,l);if(curve.IsNull())throw std::runtime_error("Support edge has no surface curve");uv=curve->Value(t);}else uv=BRep_Tool::Parameters(TopoDS::Vertex(support),face);
                    n+=supportNormal044b(face,uv.X(),uv.Y()).Normalized();}}
            if(n.Magnitude()<1e-9)throw std::runtime_error("Support normal is ambiguous at station "+std::to_string(kept[i])+" (opposed boundary faces)");n.Normalize();if(flip)n.Reverse();
            gp_Vec z=stations[i].tangent;if(project)z-=n*z.Dot(n);if(z.Magnitude()<1e-9)throw std::runtime_error("Path tangent is parallel to the support normal at station "+std::to_string(kept[i]));z.Normalize();
            gp_Vec x=n-z*n.Dot(z);if(x.Magnitude()<1e-9)throw std::runtime_error("Path tangent is parallel to the support normal at station "+std::to_string(kept[i]));x.Normalize();zs[i]=z;xs[i]=x;origins[i]=project?q.Translated(n*offsetMm):stations[i].point;
            total+=deviation;if(deviation>worst){worst=deviation;worstStation=kept[i];}
        }
        if(limit>0&&worst>limit)throw std::runtime_error("Station "+std::to_string(worstStation)+" lies "+std::to_string(worst)+" mm from the support (max_deviation_mm "+std::to_string(limit)+")");
        surfaceReceipt.set("support_faces",V::makeNum((double)faces.size()));surfaceReceipt.set("support_source",V::makeStr(own?"support_input":"path_feature"));surfaceReceipt.set("projected",V::makeBool(project));surfaceReceipt.set("offset_mm",V::makeNum(offsetMm));surfaceReceipt.set("flip_normal",V::makeBool(flip));
        surfaceReceipt.set("max_deviation_mm",V::makeNum(worst));surfaceReceipt.set("mean_deviation_mm",V::makeNum(total/(double)stations.size()));surfaceReceipt.set("max_deviation_station",V::makeNum((double)worstStation));surfaceReceipt.set("boundary_stations",V::makeNum((double)boundary));surfaceReceipt.set("deviation_scope",V::makeStr("exact_nearest_point_from_path_station_to_selected_support_faces"));
    }else for(size_t i=0;i<stations.size();++i){origins[i]=stations[i].point;zs[i]=stations[i].tangent;if(orientation=="x_hint"){gp_Vec x=hint-zs[i]*hint.Dot(zs[i]);if(x.Magnitude()<1e-10)throw std::runtime_error("Pattern x_hint parallel to curve tangent at station "+std::to_string(kept[i]));xs[i]=x.Normalized();}}
    std::vector<int> seedIndex(seedRefs.size(),-1);for(size_t r=0;r<seedRefs.size();++r)if(selectable(seedRefs[r].status)&&!seedRefs[r].shape.IsNull()){seedIndex[r]=indexOf(input,seedRefs[r].shape,refType(seedRefs[r].kind));if(seedIndex[r]<0)throw std::runtime_error("Seed reference is not part of the seed B-rep: "+seedRefs[r].name);}
    std::vector<TopoDS_Shape> copies;copies.reserve(stations.size());std::vector<Ref> copiedRefs;V frames=V::makeArr();
    for(size_t i=0;i<stations.size();++i){
        gp_Trsf place;if(orientation=="translate")place.SetTranslation(gp_Vec(origins[i].XYZ()));else place.SetDisplacement(gp_Ax3(gp::Origin(),gp::DZ(),gp::DX()),gp_Ax3(origins[i],gp_Dir(zs[i]),gp_Dir(xs[i])));
        const TopoDS_Shape copy=input.Moved(TopLoc_Location(place),true);copies.push_back(copy);
        if(scheme=="copies"||std::find(named.begin(),named.end(),kept[i])!=named.end()){const auto faces=shapes(copy,TopAbs_FACE),edges=shapes(copy,TopAbs_EDGE);
            for(size_t r=0;r<seedRefs.size();++r){Ref ref=seedRefs[r];ref.name="copy."+std::to_string(kept[i])+"."+ref.name;if(seedIndex[r]>=0){ref.shape=(ref.kind=="edge"?edges:faces).at((size_t)seedIndex[r]);ref.seed=V{};}copiedRefs.push_back(ref);}}
        if(frames.arr.size()<64){V row=V::makeObj();row.set("station",V::makeNum((double)kept[i]));row.set("arc_length_mm",V::makeNum(stations[i].s));row.set("origin_mm",xyz(origins[i]));row.set("tangent",vec(zs[i].XYZ()));if(orientation!="translate")row.set("x_axis",vec(xs[i].XYZ()));frames.arr.push_back(row);}
    }
    receipt=V::makeObj();receipt.set("schema",V::makeStr("curve_pattern_044b"));receipt.set("path_source",V::makeStr(source));receipt.set("path_edges",V::makeNum((double)path.edges.size()));receipt.set("closed",V::makeBool(path.closed));receipt.set("closed_spacing",V::makeBool(loop));receipt.set("path_length_mm",V::makeNum(path.length));receipt.set("max_chain_gap_mm",V::makeNum(path.gap));receipt.set("reversed",V::makeBool(flag044b(request,"reverse")));
    receipt.set("spacing",V::makeStr("arc_length"));receipt.set("spacing_mode",V::makeStr(spacing));receipt.set("stations",V::makeNum((double)n));receipt.set("copies",V::makeNum((double)kept.size()));V skips=V::makeArr();for(size_t i:skipped)skips.arr.push_back(V::makeNum((double)i));receipt.set("skipped",skips);
    receipt.set("pitch_mm",V::makeNum(pitch));if(hasPitch){receipt.set("requested_pitch_mm",need(request,"pitch_mm"));receipt.set("remainder_mm",V::makeNum(remainder));}receipt.set("start_margin_mm",V::makeNum(loop?0:start));receipt.set("end_margin_mm",V::makeNum(loop?0:end));if(loop)receipt.set("phase_mm",V::makeNum(phase));
    receipt.set("orientation",V::makeStr(orientation));if(orientation=="frenet")receipt.set("frenet_carried_stations",V::makeNum((double)carried));if(surface)receipt.set("surface",surfaceReceipt);
    receipt.set("reference_scheme",V::makeStr(scheme));receipt.set("semantic_references",V::makeNum((double)semantic));if(scheme=="compact"){V list=V::makeArr();for(size_t i:named)list.arr.push_back(V::makeNum((double)i));receipt.set("named_copies",list);}
    receipt.set("copy_face_stride",V::makeNum((double)seedFaces.size()));receipt.set("copy_edge_stride",V::makeNum((double)seedEdges.size()));receipt.set("copy_order",V::makeStr("ascending_station_excluding_skip"));receipt.set("shared_seed_geometry",V::makeBool(true));receipt.set("fused",V::makeBool(false));
    receipt.set("station_frames",frames);receipt.set("station_frames_truncated",V::makeBool(stations.size()>frames.arr.size()));
    refs=std::move(copiedRefs);return compound(copies);
    }catch(const Standard_Failure& error){throw std::runtime_error(std::string("Curve pattern kernel failure: ")+(error.GetMessageString()?error.GetMessageString():"OCCT exception"));}
}
// Located copies of the validated positive seed: each copy keeps the seed's
// volume, so solids are counted here and the compound is measured once.
V finishCurvePattern044b(const TopoDS_Shape& shape,const std::vector<Ref>& refs,const V& receipt){
    const auto solids=shapes(shape,TopAbs_SOLID);if(solids.empty()||solids.size()>2048)throw std::runtime_error("Curve pattern needs 1..2048 solids");
    V out=generatedShapeResult(shape,false,[&](const std::string& encoded){return topologyOf(shape,encoded,refs);},true);V report=need(out,"report");report.set("curve_pattern",receipt);out.set("report",report);return out;
}
}
