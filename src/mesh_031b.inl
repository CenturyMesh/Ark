// 031B triangle-authoritative mesh tools. Included inside mesh_model.cpp's
// private namespace, reusing its input limits and topology validation.
constexpr double meshContactTolerance = 1.0e-7;

Vec3 closestTrianglePoint(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 ab=b-a, ac=c-a, ap=p-a;
    const double d1=ab.dot(ap), d2=ac.dot(ap);
    if(d1<=0&&d2<=0)return a;
    const Vec3 bp=p-b;const double d3=ab.dot(bp),d4=ac.dot(bp);
    if(d3>=0&&d4<=d3)return b;
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0)return a+ab*(d1/(d1-d3));
    const Vec3 cp=p-c;const double d5=ab.dot(cp),d6=ac.dot(cp);
    if(d6>=0&&d5<=d6)return c;
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0)return a+ac*(d2/(d2-d6));
    const double va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const double denominator=va+vb+vc;
    if(!(denominator>0)||!std::isfinite(denominator))throw std::runtime_error("Mesh proximity is numerically ill-conditioned");
    return a+ab*(vb/denominator)+ac*(vc/denominator);
}

Vec3 closestSegmentPoint(Vec3 p,Vec3 a,Vec3 b){
    const Vec3 d=b-a;return a+d*std::clamp((p-a).dot(d)/d.lensq(),0.,1.);
}

// Emit all types of triangle contact witness, including coplanar edge crossing.
// Legal indexed adjacency is removed only from the measured contact simplex.
bool meshPairCrosses(const Mesh& mesh,size_t ia,size_t ib){
    constexpr double eps=meshContactTolerance,eps2=eps*eps;
    Vec3 a[3],b[3];std::vector<u32> shared;
    for(int i=0;i<3;++i){a[i]=at(mesh,mesh.idx[3*ia+i]);b[i]=at(mesh,mesh.idx[3*ib+i]);}
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)if(mesh.idx[3*ia+i]==mesh.idx[3*ib+j])shared.push_back(mesh.idx[3*ia+i]);
    if(shared.size()==3)return true; // Duplicate triangle, including reversed winding.
    auto unexpected=[&](Vec3 p){
        if(shared.empty())return true;
        if(shared.size()==1)return (p-at(mesh,shared[0])).lensq()>eps2;
        return (p-closestSegmentPoint(p,at(mesh,shared[0]),at(mesh,shared[1]))).lensq()>eps2;
    };
    auto pierces=[&](Vec3 start,Vec3 end,Vec3* tri){
        const Vec3 normal=(tri[1]-tri[0]).cross(tri[2]-tri[0]).normalized();
        const double x=normal.dot(start-tri[0]),y=normal.dot(end-tri[0]);
        if((x>eps&&y>eps)||(x<-eps&&y<-eps)||std::abs(x-y)<=1e-15*std::max({1.,std::abs(x),std::abs(y)}))return false;
        const double t=x/(x-y);if(t<0||t>1)return false;
        const Vec3 p=start+(end-start)*t;
        return (p-closestTrianglePoint(p,tri[0],tri[1],tri[2])).lensq()<=eps2&&unexpected(p);
    };
    for(int i=0;i<3;++i){
        if((a[i]-closestTrianglePoint(a[i],b[0],b[1],b[2])).lensq()<=eps2&&unexpected(a[i]))return true;
        if((b[i]-closestTrianglePoint(b[i],a[0],a[1],a[2])).lensq()<=eps2&&unexpected(b[i]))return true;
        if(pierces(a[i],a[(i+1)%3],b)||pierces(b[i],b[(i+1)%3],a))return true;
    }
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){
        const Vec3 p=a[i],q=b[j],d=a[(i+1)%3]-p,e=b[(j+1)%3]-q,r=p-q,n=d.cross(e);
        const double dd=d.lensq(),ee=e.lensq(),de=d.dot(e),er=e.dot(r),dr=d.dot(r),den=n.lensq();
        double s=den>0?std::clamp(e.cross(r).dot(n)/den,0.,1.):0;
        double t=(de*s+er)/ee;
        if(t<0){t=0;s=std::clamp(-dr/dd,0.,1.);}else if(t>1){t=1;s=std::clamp((de-dr)/dd,0.,1.);}
        const Vec3 pa=p+d*s,pb=q+e*t;
        if((pa-pb).lensq()<=eps2&&(unexpected(pa)||unexpected(pb)))return true;
    }
    return false;
}

V meshSelfCheck(const Mesh& mesh){
    const auto audit=analyzeMesh(mesh);
    if(audit.invalidIndices||audit.nonFiniteVertices||audit.degenerateTriangles||!mesh.triCount())throw std::runtime_error("Self-intersection diagnosis requires finite nondegenerate indexed triangles");
    if(mesh.triCount()>kOperationTriangles)throw std::runtime_error("Self-intersection diagnosis accepts at most 12000 triangles");
    struct Box{Vec3 low,high;size_t triangle;};std::vector<Box> boxes;
    for(size_t t=0;t<mesh.triCount();++t){Box box{at(mesh,mesh.idx[3*t]),at(mesh,mesh.idx[3*t]),t};for(int i=1;i<3;++i){const Vec3 p=at(mesh,mesh.idx[3*t+i]);box.low={std::min(box.low.x,p.x),std::min(box.low.y,p.y),std::min(box.low.z,p.z)};box.high={std::max(box.high.x,p.x),std::max(box.high.y,p.y),std::max(box.high.z,p.z)};}boxes.push_back(box);}
    std::sort(boxes.begin(),boxes.end(),[](const Box& a,const Box& b){return a.low.x==b.low.x?a.triangle<b.triangle:a.low.x<b.low.x;});
    size_t candidates=0,contacts=0;V examples=V::makeArr();
    for(size_t i=0;i<boxes.size();++i)for(size_t j=i+1;j<boxes.size();++j){
        const auto& a=boxes[i];const auto& b=boxes[j];
        if(b.low.x>a.high.x+meshContactTolerance)break;
        if(b.low.y>a.high.y+meshContactTolerance||a.low.y>b.high.y+meshContactTolerance||b.low.z>a.high.z+meshContactTolerance||a.low.z>b.high.z+meshContactTolerance)continue;
        if(++candidates>1000000)throw std::runtime_error("Self-intersection diagnosis exceeds 1000000 candidate pairs; no partial result published");
        if(meshPairCrosses(mesh,a.triangle,b.triangle)){
            ++contacts;if(examples.arr.size()<32){V row=V::makeObj();row.set("triangle_a",number((double)std::min(a.triangle,b.triangle)));row.set("triangle_b",number((double)std::max(a.triangle,b.triangle)));examples.arr.push_back(std::move(row));}
        }
    }
    V report=V::makeObj();report.set("self_intersections_checked",V::makeBool(true));report.set("self_intersection_pairs",number((double)contacts));report.set("self_intersection_examples",examples);report.set("examples_complete",V::makeBool(contacts<=32));report.set("candidate_triangle_pairs",number((double)candidates));report.set("check_complete",V::makeBool(true));report.set("contact_tolerance_mm",number(meshContactTolerance));report.set("method",V::makeStr("triangle_contacts_excluding_shared_indexed_simplex"));report.set("authority",V::makeStr("indexed_triangle_mesh"));report.set("exact_geometry",V::makeBool(false));report.set("manufacturing_qualified",V::makeBool(false));return report;
}

void requireNoMeshCrossings(const Mesh& mesh,const char* context,V* report=nullptr){
    V checked=meshSelfCheck(mesh);
    if(checked.find("self_intersection_pairs")->num!=0)throw std::runtime_error(std::string(context)+" has self-intersections or non-adjacent triangle contact");
    if(report)for(auto& item:checked.obj)report->set(item.first,item.second);
}

double meshParameter(const Feature& feature,const ExprEnv& env,const char* key,double fallback,double low,double high,bool required=false){
    if(required&&!feature.p.count(key))throw std::runtime_error(std::string("Missing mesh parameter: ")+key);
    const std::string defaultValue=std::to_string(fallback);
    bool valid=false;const double value=evalExpr(feature.get(key,defaultValue.c_str()),env,&valid);
    if(!valid||!std::isfinite(value)||value<low||value>high)throw std::runtime_error(std::string("Mesh parameter outside supported range: ")+key);
    return value;
}

using MeshEdge=std::pair<u32,u32>;
std::map<MeshEdge,std::vector<std::pair<u32,u32>>> meshDirectedEdges(const Mesh& mesh){
    std::map<MeshEdge,std::vector<std::pair<u32,u32>>> edges;
    for(size_t t=0;t<mesh.triCount();++t)for(int k=0;k<3;++k){const auto a=mesh.idx[3*t+k],b=mesh.idx[3*t+(k+1)%3];edges[std::minmax(a,b)].push_back({a,b});}return edges;
}

void requireSingleVertexFans(const Mesh& mesh){
    // Edge-manifoldness alone permits two shells to share only one indexed
    // vertex. Each used vertex must also have a connected disk/half-disk fan.
    std::vector<std::map<u32,std::vector<size_t>>> rings(mesh.vertCount());
    for(size_t t=0;t<mesh.triCount();++t)for(int k=0;k<3;++k){const auto vertex=mesh.idx[3*t+k];rings[vertex][mesh.idx[3*t+(k+1)%3]].push_back(t);rings[vertex][mesh.idx[3*t+(k+2)%3]].push_back(t);}
    for(const auto& ring:rings){
        if(ring.empty())continue;std::map<size_t,std::vector<size_t>> adjacent;size_t boundary=0;
        for(const auto& edge:ring){if(edge.second.size()==1){++boundary;adjacent[edge.second[0]];}else if(edge.second.size()==2){adjacent[edge.second[0]].push_back(edge.second[1]);adjacent[edge.second[1]].push_back(edge.second[0]);}else throw std::runtime_error("Advanced mesh edit requires a manifold vertex fan");}
        if(boundary!=0&&boundary!=2)throw std::runtime_error("Advanced mesh edit requires a manifold vertex fan");
        std::set<size_t> seen;std::vector<size_t> pending{adjacent.begin()->first};
        while(!pending.empty()){const auto triangle=pending.back();pending.pop_back();if(!seen.insert(triangle).second)continue;for(auto neighbor:adjacent.at(triangle))pending.push_back(neighbor);}
        if(seen.size()!=adjacent.size())throw std::runtime_error("Advanced mesh edit rejects disconnected vertex fans");
    }
}

std::vector<u32> explicitBoundaryLoop(const Mesh& mesh,const V& spec,const char* key){
    const auto* value=spec.find(key);
    if(!value||value->t!=V::Arr||value->arr.size()<3||value->arr.size()>256)throw std::runtime_error(std::string(key)+" requires 3..256 ordered boundary vertices");
    std::vector<u32> loop;std::set<u32> seen;const auto edges=meshDirectedEdges(mesh);
    for(const auto& item:value->arr){if(item.t!=V::Num||!std::isfinite(item.num)||item.num<0||item.num>=mesh.vertCount()||std::floor(item.num)!=item.num||!seen.insert((u32)item.num).second)throw std::runtime_error("Boundary loop contains invalid or duplicate index");loop.push_back((u32)item.num);}
    for(size_t i=0;i<loop.size();++i){const auto a=loop[i],b=loop[(i+1)%loop.size()];const auto found=edges.find(std::minmax(a,b));if(found==edges.end()||found->second.size()!=1||found->second.front()!=std::pair<u32,u32>{a,b})throw std::runtime_error(std::string(key)+" must follow the complete existing oriented boundary loop");}
    // A manifold vertex can still join two disconnected fans. Require precisely
    // one incoming and outgoing boundary edge at every selected vertex.
    for(auto vertex:loop){size_t degree=0;for(const auto& edge:edges)if(edge.second.size()==1&&(edge.first.first==vertex||edge.first.second==vertex))++degree;if(degree!=2)throw std::runtime_error("Boundary loop touches an ambiguous vertex fan");}
    return loop;
}

Vec3 nearestMeshPoint(Vec3 point,const Mesh& reference,size_t& terms){
    double best=1e300;Vec3 nearest;
    for(size_t t=0;t<reference.triCount();++t){if(++terms>12000000)throw std::runtime_error("Mesh proximity exceeds 12000000 point-triangle terms; no partial result published");const Vec3 q=closestTrianglePoint(point,at(reference,reference.idx[3*t]),at(reference,reference.idx[3*t+1]),at(reference,reference.idx[3*t+2]));const double d=(q-point).lensq();if(d<best){best=d;nearest=q;}}
    return nearest;
}

double sampledMeshDeviation(const Mesh& source,const Mesh& result,size_t& terms){
    double maximum=0;
    auto directed=[&](const Mesh& a,const Mesh& b){for(size_t t=0;t<a.triCount();++t){Vec3 p[3]={at(a,a.idx[3*t]),at(a,a.idx[3*t+1]),at(a,a.idx[3*t+2])};for(Vec3 sample:{p[0],p[1],p[2],(p[0]+p[1])*0.5,(p[1]+p[2])*0.5,(p[2]+p[0])*0.5,(p[0]+p[1]+p[2])/3.})maximum=std::max(maximum,(sample-nearestMeshPoint(sample,b,terms)).len());}};
    directed(source,result);directed(result,source);return maximum;
}

Mesh remeshProtected(Mesh mesh,double length,int iterations,V& report){
    const size_t originalVertices=mesh.vertCount();size_t splits=0;
    for(int pass=0;pass<iterations;++pass){
        const auto edges=meshDirectedEdges(mesh);std::vector<MeshEdge> split;
        for(const auto& edge:edges)if((at(mesh,edge.first.first)-at(mesh,edge.first.second)).len()>length)split.push_back(edge.first);
        if(split.empty())break;
        for(const auto& edge:split){
            if(mesh.vertCount()+1>kMeshModelMaxVertices)throw std::runtime_error("Remesh exceeds 12288 vertices");
            const u32 midpoint=(u32)mesh.vertCount();mesh.addVertex((at(mesh,edge.first)+at(mesh,edge.second))*0.5);
            std::vector<u32> triangles;triangles.reserve(mesh.idx.size()+6);size_t adjacent=0;
            for(size_t t=0;t<mesh.triCount();++t){bool divided=false;for(int k=0;k<3;++k){const auto a=mesh.idx[3*t+k],b=mesh.idx[3*t+(k+1)%3],c=mesh.idx[3*t+(k+2)%3];if(std::minmax(a,b)==std::minmax(edge.first,edge.second)){triangles.insert(triangles.end(),{a,midpoint,c,midpoint,b,c});divided=true;++adjacent;break;}}if(!divided)triangles.insert(triangles.end(),mesh.idx.begin()+3*t,mesh.idx.begin()+3*t+3);}
            if(!adjacent)throw std::runtime_error("Remesh lost a protected edge");
            if(triangles.size()/3>kMeshModelMaxTriangles)throw std::runtime_error("Remesh exceeds 4096 output triangles; use a larger target length");
            mesh.idx=std::move(triangles);++splits;
        }
    }
    double longest=0;for(const auto& edge:meshDirectedEdges(mesh))longest=std::max(longest,(at(mesh,edge.first.first)-at(mesh,edge.first.second)).len());
    report.set("remesh_model",V::makeStr("conforming_midpoint_refinement"));report.set("target_length_mm",number(length));report.set("maximum_edge_mm",number(longest));report.set("target_reached",V::makeBool(longest<=length*(1+1e-6)));report.set("split_edges",number((double)splits));report.set("retained_original_vertices",number((double)originalVertices));report.set("boundary_and_crease_geometry_preserved",V::makeBool(true));report.set("protection_model",V::makeStr("all_original_edges_and_vertices_retained_or_collinearly_subdivided"));
    return mesh;
}

bool isAdvancedMeshEdit(const std::string& op){return op=="remesh"||op=="simplify"||op=="fill_nonplanar"||op=="bridge"||op=="shrinkwrap";}

Mesh advancedMeshEdit(Mesh mesh,const Feature& feature,const ExprEnv& env,V& report){
    const std::string op=feature.nativeSpec.find("op")->str;
    std::set<std::string> fields{"op","source_topology","require_closed"};
    std::set<std::string> parameters{"sample"};
    if(op=="remesh"){parameters.insert("target_length");parameters.insert("iterations");}
    if(op=="simplify"){parameters.insert("ratio");parameters.insert("max_error");}
    if(op=="fill_nonplanar"){fields.insert("loop");fields.insert("method");}
    if(op=="bridge"){fields.insert("loop_a");fields.insert("loop_b");}
    if(op=="shrinkwrap"){fields.insert("reference_mesh");fields.insert("vertices");parameters.insert("max_distance");parameters.insert("strength");}
    for(const auto& field:feature.nativeSpec.obj)if(!fields.count(field.first))throw std::runtime_error("Unknown "+op+" mesh field: "+field.first);
    for(const auto& parameter:feature.p)if(!parameters.count(parameter.first))throw std::runtime_error("Unknown "+op+" mesh parameter: "+parameter.first);
    const auto before=analyzeMesh(mesh);requireFiniteManifold(before,"Advanced mesh edit input");requireSingleVertexFans(mesh);requireNoMeshCrossings(mesh,"Advanced mesh edit input");
    bool requireClosed=before.closedOriented();
    if(const auto* closed=feature.nativeSpec.find("require_closed")){if(closed->t!=V::Bool)throw std::runtime_error("require_closed must be boolean");requireClosed=requireClosed||closed->b;}
    if(op=="remesh"){
        const double length=meshParameter(feature,env,"target_length",1,1e-6,1e6,true),iterations=meshParameter(feature,env,"iterations",3,1,6);
        if(iterations!=std::floor(iterations))throw std::runtime_error("Remesh iterations must be integer 1..6");
        mesh=remeshProtected(std::move(mesh),length,(int)iterations,report);
    }else if(op=="simplify"){
        const double ratio=meshParameter(feature,env,"ratio",1,.05,1,true),cap=meshParameter(feature,env,"max_error",0,0,1e4,true);
        const Mesh source=mesh;mesh=decimate(std::move(mesh),ratio);
        size_t terms=0;const double measured=sampledMeshDeviation(source,mesh,terms);
        if(measured>cap)throw std::runtime_error("Simplify sampled bidirectional deviation "+std::to_string(measured)+" mm exceeds explicit max_error "+std::to_string(cap)+" mm; result rejected");
        const size_t target=std::max<size_t>(1,(size_t)std::floor(ratio*source.triCount()+1e-9));
        report.set("simplification_model",V::makeStr("interior_quadric_midpoint_collapse"));report.set("target_triangles",number((double)target));report.set("target_reached",V::makeBool(mesh.triCount()<=target));report.set("sampled_bidirectional_deviation_mm",number(measured));report.set("max_error_mm",number(cap));report.set("error_cap_satisfied",V::makeBool(true));report.set("global_hausdorff_bound",V::makeBool(false));report.set("deviation_sampling",V::makeStr("both_surfaces_each_triangle_vertices_edge_midpoints_and_centroid"));report.set("point_triangle_terms",number((double)terms));report.set("boundary_preserved",V::makeBool(true));
    }else if(op=="fill_nonplanar"){
        const auto* method=feature.nativeSpec.find("method");if(!method||method->t!=V::Str||method->str!="centroid_fan")throw std::runtime_error("Nonplanar mesh fill requires explicit method:centroid_fan");
        const auto loop=explicitBoundaryLoop(mesh,feature.nativeSpec,"loop");
        if(mesh.vertCount()+1>kMeshModelMaxVertices||mesh.triCount()+loop.size()>kMeshModelMaxTriangles)throw std::runtime_error("Nonplanar mesh fill exceeds output capacity");
        Vec3 center,normal;for(size_t i=0;i<loop.size();++i){center=center+at(mesh,loop[i]);normal=normal+at(mesh,loop[i]).cross(at(mesh,loop[(i+1)%loop.size()]));}center=center/(double)loop.size();
        if(normal.len()<=1e-12)throw std::runtime_error("Nonplanar mesh fill boundary has no measurable mean normal");
        normal=normal.normalized();double deviation=0;for(auto vertex:loop)deviation=std::max(deviation,std::abs((at(mesh,vertex)-center).dot(normal)));
        const u32 index=(u32)mesh.vertCount();mesh.addVertex(center);
        for(size_t i=0;i<loop.size();++i)mesh.addTri3(loop[(i+1)%loop.size()],loop[i],index);
        report.set("fill_model",V::makeStr("explicit_nonplanar_centroid_fan"));report.set("filled_boundary_vertices",number((double)loop.size()));report.set("boundary_mean_plane_deviation_mm",number(deviation));report.set("curvature_continuity",V::makeStr("piecewise_planar_C0"));
    }else if(op=="bridge"){
        const auto a=explicitBoundaryLoop(mesh,feature.nativeSpec,"loop_a"),b=explicitBoundaryLoop(mesh,feature.nativeSpec,"loop_b");
        if(a.size()!=b.size())throw std::runtime_error("Mesh bridge requires equally sized boundary loops with explicit correspondence");
        std::set<u32> first(a.begin(),a.end());for(auto vertex:b)if(first.count(vertex))throw std::runtime_error("Mesh bridge requires disjoint boundary loops");
        if(mesh.triCount()+a.size()*2>kMeshModelMaxTriangles)throw std::runtime_error("Mesh bridge exceeds 4096 output triangles");
        // Both supplied loops follow their source winding. Reverse B while
        // retaining its explicit first vertex, so each old boundary cancels.
        for(size_t i=0;i<a.size();++i){const auto ai=a[i],aj=a[(i+1)%a.size()],bi=b[(b.size()-i)%b.size()],bj=b[(b.size()-i-1)%b.size()];mesh.addTri3(aj,ai,bi);mesh.addTri3(aj,bi,bj);}
        report.set("bridge_model",V::makeStr("explicit_equal_count_boundary_strip"));report.set("bridged_edges_per_loop",number((double)a.size()));report.set("correspondence",V::makeStr("loop_a_forward_loop_b_reverse_first_vertices_paired"));
    }else if(op=="shrinkwrap"){
        const auto* payload=feature.nativeSpec.find("reference_mesh");if(!payload)throw std::runtime_error("Shrinkwrap requires immutable native.reference_mesh payload");
        const Mesh reference=meshFromPayload(*payload);const auto referenceAudit=analyzeMesh(reference);requireFiniteManifold(referenceAudit,"Shrinkwrap reference");requireSingleVertexFans(reference);requireNoMeshCrossings(reference,"Shrinkwrap reference");
        const double maximum=meshParameter(feature,env,"max_distance",0,1e-7,1e6,true),strength=meshParameter(feature,env,"strength",1,1e-6,1);
        std::set<u32> vertices;
        if(const auto* selected=feature.nativeSpec.find("vertices")){if(selected->t!=V::Arr||selected->arr.empty())throw std::runtime_error("Shrinkwrap vertices must be nonempty array");for(const auto& vertex:selected->arr)if(vertex.t!=V::Num||!std::isfinite(vertex.num)||vertex.num<0||vertex.num>=mesh.vertCount()||std::floor(vertex.num)!=vertex.num||!vertices.insert((u32)vertex.num).second)throw std::runtime_error("Shrinkwrap vertices contain invalid/duplicate index");}
        else for(u32 i=0;i<mesh.vertCount();++i)vertices.insert(i);
        size_t terms=0;double moved=0;
        for(auto vertex:vertices){const Vec3 old=at(mesh,vertex),target=nearestMeshPoint(old,reference,terms);const double distance=(target-old).len();if(distance>maximum)throw std::runtime_error("Shrinkwrap source vertex exceeds max_distance; result rejected");const Vec3 next=old+(target-old)*strength;mesh.pos[3*vertex]=(f32)next.x;mesh.pos[3*vertex+1]=(f32)next.y;mesh.pos[3*vertex+2]=(f32)next.z;moved=std::max(moved,(next-old).len());}
        report.set("projection_model",V::makeStr("nearest_point_on_retained_triangle_reference"));report.set("reference_immutable",V::makeBool(true));report.set("reference_authority",V::makeStr("indexed_triangle_mesh"));report.set("projected_vertices",number((double)vertices.size()));report.set("maximum_vertex_displacement_mm",number(moved));report.set("max_distance_mm",number(maximum));report.set("strength",number(strength));report.set("point_triangle_terms",number((double)terms));
    }
    mesh.computeNormals();const auto after=analyzeMesh(mesh);requireFiniteManifold(after,"Advanced mesh edit result");requireSingleVertexFans(mesh);
    if(requireClosed&&!after.closedOriented())throw std::runtime_error("Advanced mesh edit did not produce required closed oriented mesh");
    if(before.closedOriented()&&before.signedVolume*after.signedVolume<=0)throw std::runtime_error("Advanced mesh edit collapsed or reversed enclosed volume");
    requireNoMeshCrossings(mesh,"Advanced mesh edit result",&report);
    report.set("residual_boundary_edges",number((double)after.boundaryEdges));return mesh;
}
