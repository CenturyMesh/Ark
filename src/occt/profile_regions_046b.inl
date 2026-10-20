// 046B profile regions (B.02/B.03). Several closed sketch loops become planar
// faces by containment depth: an even depth bounds material, an odd depth is a
// hole of its immediate container (even-odd). Edge ends join only at a shared
// vertex or within the sum of their own vertex tolerances. Touching, crossing,
// branched, zero-area and self-intersecting loops reject; open chains reject
// unless regions are selected explicitly. Loops are ordered by depth, then by
// their first authored profile edge; regions follow their outer loops. No
// tolerance is enlarged and no loop is dropped silently.
struct RegionEdge046B{std::string name,entity;TopoDS_Edge edge;int source=-1;gp_Pnt ends[2];double tolerance[2]{0,0};};
struct RegionLoop046B{std::vector<std::pair<size_t,bool>> walk;TopoDS_Wire wire;TopoDS_Face face;double area=0,tolerance=0;int depth=0,parent=-1,index=-1,region=-1;size_t first=0;Bnd_Box box;std::string entities;};
struct ProfileRegions046B{std::vector<TopoDS_Face> faces;std::vector<std::pair<std::string,TopoDS_Edge>> edges;std::vector<std::string> labels;V receipt;};

std::string regionNumber046B(double value){std::ostringstream out;out.precision(9);out<<value;return out.str();}
std::string regionEntities046B(const std::vector<RegionEdge046B>& edges,const std::vector<size_t>& members){
    std::vector<std::string> names;for(size_t i:members)if(std::find(names.begin(),names.end(),edges[i].entity)==names.end())names.push_back(edges[i].entity);
    std::string out;for(const auto& name:names)out+=(out.empty()?"":", ")+name;return out;
}
void requireOneRegion046B(const ProfileRegions046B& regions,const std::string& consumer){
    if(regions.faces.size()==1)return;std::string outers;for(const auto& label:regions.labels)outers+=(outers.empty()?"":"; ")+label;
    throw std::runtime_error("Profile selects "+std::to_string(regions.faces.size())+" separate regions (outer loops: "+outers+"); "+consumer+" publishes one solid. Select one region with native.regions:[{entity:name}|{point:[x,y]}] or author each region as its own feature");
}

ProfileRegions046B profileRegions046B(const V& profile){
    const char* stage="profile regions setup";
    try{
    const auto m=frame(profile);const V& entities=need(profile,"entities");
    if(entities.t!=V::Arr||entities.arr.empty()||entities.arr.size()>256)throw std::runtime_error("Profile needs 1..256 entities");
    const V& spec=need(profile,"regions");if(spec.t!=V::Obj)throw std::runtime_error("Profile regions must be an object");
    for(const auto& field:spec.obj)if(field.first!="select")throw std::runtime_error("Unknown profile regions field: "+field.first);
    std::vector<RegionEdge046B> edges;
    auto push=[&](const V& entity,const std::string& name,const TopoDS_Edge& edge){
        RegionEdge046B row;row.name=name;row.edge=edge;const V* label=entity.find("entity");row.entity=label&&label->t==V::Str?label->str:name;
        if(const V* source=entity.find("source");source&&source->t==V::Num)row.source=static_cast<int>(source->num);
        TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b,true);if(a.IsNull()||b.IsNull())throw std::runtime_error("Profile edge "+name+" has no end vertices");
        row.ends[0]=BRep_Tool::Pnt(a);row.ends[1]=BRep_Tool::Pnt(b);row.tolerance[0]=BRep_Tool::Tolerance(a);row.tolerance[1]=BRep_Tool::Tolerance(b);
        edges.push_back(row);if(edges.size()>1024)throw std::runtime_error("Profile regions accept at most 1024 edges");
    };
    size_t index=0;
    for(const V& e:entities.arr){
        const std::string kind=word(e,"kind"),base=e.find("name")?word(e,"name"):"side."+std::to_string(index);
        if(kind=="polyline"){
            const V& points=need(e,"points");if(points.t!=V::Arr||points.arr.size()<2||points.arr.size()>4096)throw std::runtime_error("Polyline requires 2..4096 points");
            auto xy=[](const V& p){if(p.t!=V::Arr||p.arr.size()!=2||p.arr[0].t!=V::Num||p.arr[1].t!=V::Num||!std::isfinite(p.arr[0].num)||!std::isfinite(p.arr[1].num))throw std::runtime_error("Polyline points must be finite XY");return gp_Pnt(p.arr[0].num,p.arr[1].num,0);};
            for(size_t i=1;i<points.arr.size();++i){gp_Pnt a=xy(points.arr[i-1]),b=xy(points.arr[i]);if(a.Distance(b)<=1e-8)throw std::runtime_error("Polyline has coincident consecutive points");BRepBuilderAPI_MakeEdge maker(a,b);if(!maker.IsDone())throw std::runtime_error("Polyline edge failed");push(e,base+"."+std::to_string(i-1),maker.Edge());}
        }else if(kind=="brep"){
            auto wires=shapes(readBrep(word(e,"brep_base64")),TopAbs_WIRE);if(wires.size()!=1)throw std::runtime_error("Projected profile must contain one exact wire");
            int part=0;for(BRepTools_WireExplorer walk(TopoDS::Wire(wires[0]));walk.More();walk.Next())push(e,base+"."+std::to_string(part++),walk.Current());
        }else push(e,base,authoredProfileEdge(e,kind));
        ++index;
    }
    stage="profile regions connectivity";
    const size_t n=edges.size();std::vector<size_t> slot(2*n);for(size_t i=0;i<slot.size();++i)slot[i]=i;
    auto root=[&](size_t i){while(slot[i]!=i)i=slot[i]=slot[slot[i]];return i;};
    for(size_t a=0;a<2*n;++a)for(size_t b=a+1;b<2*n;++b){const auto& ea=edges[a/2];const auto& eb=edges[b/2];if(ea.ends[a%2].Distance(eb.ends[b%2])<=ea.tolerance[a%2]+eb.tolerance[b%2])slot[root(a)]=root(b);}
    std::map<size_t,std::vector<size_t>> clusters;for(size_t s=0;s<2*n;++s)clusters[root(s)].push_back(s);
    for(const auto& cluster:clusters)if(cluster.second.size()>2){
        std::vector<size_t> members;for(size_t s:cluster.second)members.push_back(s/2);const gp_Pnt p=edges[cluster.second.front()/2].ends[cluster.second.front()%2];
        throw std::runtime_error("Profile entities "+regionEntities046B(edges,members)+" meet at one point ("+regionNumber046B(p.X())+", "+regionNumber046B(p.Y())+"); loops must not touch or branch");
    }
    auto partner=[&](size_t s)->long{const auto& members=clusters[root(s)];for(size_t other:members)if(other!=s)return static_cast<long>(other);return -1;};
    std::vector<size_t> component(n);for(size_t i=0;i<n;++i)component[i]=i;
    auto group=[&](size_t i){while(component[i]!=i)i=component[i]=component[component[i]];return i;};
    for(size_t s=0;s<2*n;++s){const long other=partner(s);if(other>=0)component[group(s/2)]=group(static_cast<size_t>(other)/2);}
    std::vector<RegionLoop046B> loops;std::vector<std::vector<size_t>> openChains;std::vector<bool> visited(n,false);
    for(size_t start=0;start<n;++start){
        if(visited[start])continue;std::vector<size_t> members;for(size_t i=0;i<n;++i)if(group(i)==group(start)){members.push_back(i);visited[i]=true;}
        bool open=false;for(size_t i:members)for(size_t end=0;end<2;++end)if(partner(2*i+end)<0)open=true;
        if(open){openChains.push_back(members);continue;}
        RegionLoop046B loop;loop.first=start;loop.entities=regionEntities046B(edges,members);size_t current=2*start+1;loop.walk.push_back({start,false});
        while(true){const long other=partner(current);if(other<0)throw std::runtime_error("Profile loop "+loop.entities+" lost its connectivity");const size_t o=static_cast<size_t>(other),edge=o/2;
            if(edge==start&&o%2==0)break;if(loop.walk.size()>members.size())throw std::runtime_error("Profile loop "+loop.entities+" does not close as one simple chain");
            const bool reversed=o%2==1;loop.walk.push_back({edge,reversed});current=reversed?2*edge:2*edge+1;}
        if(loop.walk.size()!=members.size())throw std::runtime_error("Profile loop "+loop.entities+" does not close as one simple chain");
        loops.push_back(std::move(loop));
    }
    if(loops.empty())throw std::runtime_error("Profile has no closed loop; explicitly close its curves");
    if(loops.size()>256)throw std::runtime_error("Profile regions accept at most 256 closed loops");
    stage="profile regions loop faces";
    for(auto& loop:loops){
        BRepBuilderAPI_MakeWire maker;
        for(const auto& [edge,reversed]:loop.walk){TopoDS_Edge piece=edges[edge].edge;if(reversed)piece.Reverse();maker.Add(piece);if(!maker.IsDone())throw std::runtime_error("Profile loop "+loop.entities+": edge "+edges[edge].name+" does not join within its vertex tolerances");edges[edge].edge=maker.Edge();}
        loop.wire=maker.Wire();TopoDS_Vertex first,last;TopExp::Vertices(loop.wire,first,last);
        if(first.IsNull()||last.IsNull()||!first.IsSame(last))throw std::runtime_error("Profile loop "+loop.entities+" does not close within its vertex tolerances");
        BRepBuilderAPI_MakeFace face(loop.wire,true);if(!face.IsDone())throw std::runtime_error("Profile loop "+loop.entities+" is not one planar closed curve with positive area");
        loop.face=face.Face();if(!BRepCheck_Analyzer(loop.face).IsValid())throw std::runtime_error("Profile loop "+loop.entities+" self-intersects, has zero area or is degenerate (exact face validation failed)");
        loop.area=std::abs(properties(loop.face,'a'));if(!(loop.area>1e-9))throw std::runtime_error("Profile loop "+loop.entities+" has zero area");
        BRepBndLib::Add(loop.wire,loop.box);for(const auto& vertex:shapes(loop.wire,TopAbs_VERTEX))loop.tolerance=std::max(loop.tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(vertex)));
    }
    stage="profile regions containment";
    auto sample=[&](const RegionLoop046B& loop){BRepAdaptor_Curve curve(edges[loop.walk.front().first].edge);return curve.Value((curve.FirstParameter()+curve.LastParameter())*.5);};
    auto inside=[&](const gp_Pnt& p,const RegionLoop046B& loop){BRepClass_FaceClassifier classifier(loop.face,p,Precision::Confusion());return classifier.State()==TopAbs_IN;};
    std::vector<std::vector<size_t>> containers(loops.size());
    for(size_t i=0;i<loops.size();++i)for(size_t j=i+1;j<loops.size();++j){
        Bnd_Box a=loops[i].box;a.Enlarge(loops[i].tolerance+loops[j].tolerance);if(a.IsOut(loops[j].box))continue;
        BRepExtrema_DistShapeShape distance(loops[i].wire,loops[j].wire);if(!distance.IsDone())throw std::runtime_error("Cannot measure the separation of profile loops "+loops[i].entities+" and "+loops[j].entities);
        if(distance.Value()<=loops[i].tolerance+loops[j].tolerance)throw std::runtime_error("Profile loops "+loops[i].entities+" and "+loops[j].entities+" intersect or touch (closest distance "+regionNumber046B(distance.Value())+" mm); separate them or merge them into one loop");
        if(inside(sample(loops[j]),loops[i]))containers[j].push_back(i);else if(inside(sample(loops[i]),loops[j]))containers[i].push_back(j);
    }
    for(size_t i=0;i<loops.size();++i){loops[i].depth=static_cast<int>(containers[i].size());for(size_t c:containers[i])if(loops[i].parent<0||containers[c].size()>containers[static_cast<size_t>(loops[i].parent)].size())loops[i].parent=static_cast<int>(c);}
    std::vector<size_t> order(loops.size());for(size_t i=0;i<order.size();++i)order[i]=i;
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return loops[a].depth!=loops[b].depth?loops[a].depth<loops[b].depth:loops[a].first<loops[b].first;});
    for(size_t k=0;k<order.size();++k)loops[order[k]].index=static_cast<int>(k);
    std::vector<size_t> outers;for(size_t k:order)if(loops[k].depth%2==0){loops[k].region=static_cast<int>(outers.size());outers.push_back(k);}
    for(auto& loop:loops)if(loop.depth%2==1)loop.region=loops[static_cast<size_t>(loop.parent)].region;
    for(const auto& chain:openChains)for(const auto& loop:loops){
        BRep_Builder builder;TopoDS_Compound curves;builder.MakeCompound(curves);double tolerance=loop.tolerance;for(size_t i:chain){builder.Add(curves,edges[i].edge);tolerance=std::max({tolerance,loop.tolerance+edges[i].tolerance[0],loop.tolerance+edges[i].tolerance[1]});}
        BRepExtrema_DistShapeShape distance(curves,loop.wire);if(distance.IsDone()&&distance.Value()<=tolerance)throw std::runtime_error("Open profile chain "+regionEntities046B(edges,chain)+" touches or crosses loop "+loop.entities+"; regions are bounded by closed loops only");
    }
    stage="profile regions selection";
    const V* select=spec.find("select");std::vector<int> chosen(outers.size(),-1);
    if(!select){
        if(!openChains.empty())throw std::runtime_error("Profile mixes closed loops with open chains ("+regionEntities046B(edges,openChains.front())+"); close them, mark them construction, or select regions with native.regions");
        for(size_t r=0;r<outers.size();++r)chosen[r]=-2;
    }else{
        if(select->t!=V::Arr||select->arr.empty()||select->arr.size()>64)throw std::runtime_error("Profile region selection needs 1..64 seeds");
        for(size_t k=0;k<select->arr.size();++k){
            const V& seed=select->arr[k];const std::string label="native.regions["+std::to_string(k)+"]";int region=-1;
            if(const V* at=seed.find("point")){
                if(at->t!=V::Arr||at->arr.size()!=2||at->arr[0].t!=V::Num||at->arr[1].t!=V::Num||!std::isfinite(at->arr[0].num)||!std::isfinite(at->arr[1].num))throw std::runtime_error(label+".point must be finite [x,y]");
                const gp_Pnt p(at->arr[0].num,at->arr[1].num,0);const std::string where="("+regionNumber046B(p.X())+", "+regionNumber046B(p.Y())+")";const TopoDS_Vertex vertex=BRepBuilderAPI_MakeVertex(p).Vertex();int deepest=-1;
                for(size_t i=0;i<loops.size();++i){
                    BRepExtrema_DistShapeShape distance(vertex,loops[i].wire);if(!distance.IsDone())throw std::runtime_error(label+" cannot be classified against loop "+loops[i].entities);
                    if(distance.Value()<=std::max(1e-6,loops[i].tolerance))throw std::runtime_error(label+" point "+where+" lies on loop "+loops[i].entities+"; move the seed inside one region");
                    if(inside(p,loops[i])&&(deepest<0||loops[i].depth>loops[static_cast<size_t>(deepest)].depth))deepest=static_cast<int>(i);
                }
                if(deepest<0)throw std::runtime_error(label+" point "+where+" lies outside every closed loop");
                const auto& owner=loops[static_cast<size_t>(deepest)];if(owner.depth%2==1)throw std::runtime_error(label+" point "+where+" lies inside hole loop "+owner.entities+"; seed a point inside material");
                region=owner.region;
            }else if(const V* source=seed.find("entity_index")){
                const std::string entity=seed.find("entity")&&seed.find("entity")->t==V::Str?seed.find("entity")->str:"#"+regionNumber046B(source->asNum());std::vector<size_t> owners;
                for(size_t i=0;i<loops.size();++i)for(const auto& step:loops[i].walk)if(edges[step.first].source==static_cast<int>(source->asNum())&&std::find(owners.begin(),owners.end(),i)==owners.end())owners.push_back(i);
                if(owners.empty()){for(const auto& chain:openChains)for(size_t i:chain)if(edges[i].source==static_cast<int>(source->asNum()))throw std::runtime_error(label+" entity "+entity+" is an open chain, not a region boundary");throw std::runtime_error(label+" entity "+entity+" is not a profile curve of this sketch");}
                if(owners.size()>1)throw std::runtime_error(label+" entity "+entity+" bounds several loops; use a point seed");
                const auto& owner=loops[owners.front()];if(owner.depth%2==1)throw std::runtime_error(label+" entity "+entity+" bounds hole loop "+owner.entities+" of region "+std::to_string(owner.region)+"; name its outer loop curve or use a point seed");
                region=owner.region;
            }else throw std::runtime_error(label+" needs point or entity");
            if(chosen[static_cast<size_t>(region)]>=0)throw std::runtime_error(label+" selects region "+std::to_string(region)+" already selected by native.regions["+std::to_string(chosen[static_cast<size_t>(region)])+"]");
            chosen[static_cast<size_t>(region)]=static_cast<int>(k);
        }
    }
    stage="profile regions faces";
    ProfileRegions046B out;V loopRows=V::makeArr(),regionRows=V::makeArr(),selected=V::makeArr();
    auto normalOf=[](const TopoDS_Face& face){gp_Dir normal=BRepAdaptor_Surface(face).Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();return normal;};
    for(size_t r=0;r<outers.size();++r){
        const auto& outer=loops[outers[r]];std::vector<size_t> holes;for(size_t k:order)if(loops[k].depth%2==1&&loops[k].parent==static_cast<int>(outers[r]))holes.push_back(k);
        double expected=outer.area;for(size_t h:holes)expected-=loops[h].area;
        V row=V::makeObj(),holeRows=V::makeArr();row.set("index",V::makeNum(static_cast<double>(r)));row.set("outer_loop",V::makeNum(outer.index));for(size_t h:holes)holeRows.arr.push_back(V::makeNum(loops[h].index));row.set("hole_loops",holeRows);row.set("area_mm2",V::makeNum(expected));row.set("selected",V::makeBool(chosen[r]!=-1));
        if(chosen[r]>=0)row.set("selected_by",V::makeStr("native.regions["+std::to_string(chosen[r])+"]"));regionRows.arr.push_back(row);
        if(chosen[r]==-1)continue;selected.arr.push_back(V::makeNum(static_cast<double>(r)));
        TopoDS_Face face=outer.face;
        if(!holes.empty()){
            BRepBuilderAPI_MakeFace adder(face);const gp_Dir normal=normalOf(face);
            for(size_t h:holes){TopoDS_Wire wire=BRepTools::OuterWire(loops[h].face);if(normalOf(loops[h].face).Dot(normal)>0)wire.Reverse();adder.Add(wire);if(!adder.IsDone())throw std::runtime_error("Profile hole loop "+loops[h].entities+" could not be added to region "+outer.entities);}
            face=adder.Face();
            if(!BRepCheck_Analyzer(face).IsValid())throw std::runtime_error("Profile region "+outer.entities+" with its holes failed exact face validation");
            const double area=std::abs(properties(face,'a'));if(std::abs(area-expected)>std::max(1e-9,outer.area*1e-9))throw std::runtime_error("Profile region "+outer.entities+" area "+regionNumber046B(area)+" mm2 disagrees with outer minus holes "+regionNumber046B(expected)+" mm2");
        }
        stage="profile regions frame transform";
        BRepBuilderAPI_Transform move(face,transform(m),true);if(!move.IsDone())throw std::runtime_error("Profile region frame transform failed");
        const auto faceEdges=shapes(face,TopAbs_EDGE);
        auto mapped=[&](const TopoDS_Edge& edge){
            // As the single-loop profile: locate the face's own edge, then follow the copy.
            TopoDS_Shape source;for(const auto& candidate:faceEdges)if(candidate.IsSame(edge)){source=candidate;break;}
            if(source.IsNull()){TopLoc_Location origin;double first=0,last=0;auto curve=BRep_Tool::Curve(edge,origin,first,last);if(curve.IsNull())throw std::runtime_error("Profile region edge has no exact curve handle");
                for(const auto& candidate:faceEdges){TopLoc_Location location;double a=0,b=0;auto other=BRep_Tool::Curve(TopoDS::Edge(candidate),location,a,b);if(curve==other&&origin.IsEqual(location)&&first==a&&last==b){if(!source.IsNull())throw std::runtime_error("Ambiguous exact curve origin in profile region");source=candidate;}}}
            if(source.IsNull())throw std::runtime_error("Profile region edge absent from its constructed face");
            TopoDS_Shape changed=move.ModifiedShape(source);if(changed.IsNull()&&indexOf(move.Shape(),source,TopAbs_EDGE)>=0)changed=source;if(changed.IsNull())throw std::runtime_error("Profile region edge was not retained by transform");return TopoDS::Edge(changed);};
        std::vector<size_t> members{outers[r]};members.insert(members.end(),holes.begin(),holes.end());
        for(size_t l:members)for(const auto& step:loops[l].walk)out.edges.push_back({edges[step.first].name,mapped(edges[step.first].edge)});
        out.faces.push_back(TopoDS::Face(move.Shape()));out.labels.push_back(outer.entities);
        stage="profile regions faces";
    }
    if(out.faces.empty())throw std::runtime_error("Profile region selection is empty");
    for(size_t k:order){const auto& loop=loops[k];V row=V::makeObj(),names=V::makeArr();row.set("index",V::makeNum(loop.index));row.set("depth",V::makeNum(loop.depth));row.set("role",V::makeStr(loop.depth%2?"hole":"outer"));row.set("parent_loop",loop.parent<0?V{}:V::makeNum(loops[static_cast<size_t>(loop.parent)].index));row.set("region",V::makeNum(loop.region));row.set("entities",V::makeStr(loop.entities));for(const auto& step:loop.walk)names.arr.push_back(V::makeStr(edges[step.first].name));row.set("edges",names);row.set("area_mm2",V::makeNum(loop.area));loopRows.arr.push_back(row);}
    V chains=V::makeArr();for(const auto& chain:openChains)chains.arr.push_back(V::makeStr(regionEntities046B(edges,chain)));
    out.receipt=V::makeObj();out.receipt.set("schema",V::makeStr("ark.profile-regions.046b"));out.receipt.set("containment",V::makeStr("even_odd_depth"));out.receipt.set("selection",V::makeStr(select?"explicit":"all"));out.receipt.set("loops",loopRows);out.receipt.set("regions",regionRows);out.receipt.set("selected_regions",selected);out.receipt.set("open_chains_ignored",chains);
    out.receipt.set("join_rule",V::makeStr("shared vertex or sum of vertex tolerances"));out.receipt.set("authority",V::makeStr("exact_occt_planar_faces"));
    return out;
    }catch(const Standard_Failure& failure){throw std::runtime_error(std::string("OCCT failed during ")+stage+": "+(failure.GetMessageString()?failure.GetMessageString():""));}
}
