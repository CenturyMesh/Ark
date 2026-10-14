// Display-only cache. Exact/source geometry and reference ownership are unchanged.
// Mesh uploads invalidate adjacency; pose/camera changes invalidate silhouettes.
struct BodyGlowTriangle { Vec3 normal,center; };
struct BodyGlowEdge { Vec3 a,b;std::vector<u32> faces; };
struct BodyGlowTopology { std::vector<BodyGlowTriangle> triangles;std::vector<BodyGlowEdge> edges; };
static u64 g_bodyGlowTopologyBuilds=0,g_bodyGlowBoundaryBuilds=0;
static u64 g_bodyGlowMeshSerial=~u64(0);
static std::map<size_t,BodyGlowTopology> g_bodyGlowTopologies;
struct BodyGlowBoundaryCache {
    bool valid=false,ortho=false;size_t part=0;u64 serial=0;
    Mat4 world;Vec3 eye,outward;std::vector<float> boundary;
};
static BodyGlowBoundaryCache g_bodyGlowBoundaryCache;
static const std::vector<float>& cachedSelectedBodyBoundary(size_t body){
    static const std::vector<float> empty;
    if(body>=g_instancePart.size()||body>=g_instanceWorld.size()||!displayInstance(body))return empty;
    const size_t part=g_instancePart[body];if(part>=g_cpuMeshes.size())return empty;
    if(g_bodyGlowMeshSerial!=g_displayMeshSerial){
        g_bodyGlowTopologies.clear();g_bodyGlowBoundaryCache.valid=false;g_bodyGlowMeshSerial=g_displayMeshSerial;
    }
    auto found=g_bodyGlowTopologies.find(part);
    if(found==g_bodyGlowTopologies.end()){
        BodyGlowTopology topology;const auto& mesh=g_cpuMeshes[part];
        using Point=std::array<float,3>;using Edge=std::pair<Point,Point>;
        std::map<Edge,std::vector<u32>> edges;
        for(size_t at=0;at+2<mesh.idx.size();at+=3){
            Point points[3];Vec3 vertices[3];bool valid=true;
            for(int k=0;k<3;++k){size_t index=(size_t)mesh.idx[at+k]*3;if(index+2>=mesh.pos.size()){valid=false;break;}
                points[k]={mesh.pos[index],mesh.pos[index+1],mesh.pos[index+2]};vertices[k]={points[k][0],points[k][1],points[k][2]};}
            if(!valid)continue;const auto face=(u32)topology.triangles.size();
            topology.triangles.push_back({(vertices[1]-vertices[0]).cross(vertices[2]-vertices[0]),(vertices[0]+vertices[1]+vertices[2])/3.});
            for(int k=0;k<3;++k){auto a=points[k],b=points[(k+1)%3];if(b<a)std::swap(a,b);edges[{a,b}].push_back(face);}
        }
        topology.edges.reserve(edges.size());
        for(auto& item:edges)topology.edges.push_back({{item.first.first[0],item.first.first[1],item.first.first[2]},
            {item.first.second[0],item.first.second[1],item.first.second[2]},std::move(item.second)});
        found=g_bodyGlowTopologies.emplace(part,std::move(topology)).first;++g_bodyGlowTopologyBuilds;
    }
    const auto& world=g_instanceWorld[body];const auto eye=camEye(),outward=cameraFrame(yaw,pitch,cameraRoll).outward;
    auto& cached=g_bodyGlowBoundaryCache;
    auto equalPoint=[](Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;};
    bool sameWorld=cached.valid;if(sameWorld)for(int i=0;i<16;++i)if(cached.world.m[i]!=world.m[i]){sameWorld=false;break;}
    if(cached.valid&&cached.part==part&&cached.serial==g_displayMeshSerial&&sameWorld&&cached.ortho==g_cameraOrtho&&
        equalPoint(cached.eye,eye)&&equalPoint(cached.outward,outward))return cached.boundary;
    cached={};cached.valid=true;cached.part=part;cached.serial=g_displayMeshSerial;cached.world=world;cached.eye=eye;cached.outward=outward;cached.ortho=g_cameraOrtho;
    const auto inverse=world.inverted();const auto localEye=inverse.transformPoint(eye),localOutward=inverse.transformDir(outward);
    const double determinant=world.m[0]*(world.m[5]*world.m[10]-world.m[9]*world.m[6])-world.m[4]*(world.m[1]*world.m[10]-world.m[9]*world.m[2])+world.m[8]*(world.m[1]*world.m[6]-world.m[5]*world.m[2]);
    const double orientation=determinant<0?-1.:1.;const auto& topology=found->second;std::vector<bool> front(topology.triangles.size());
    for(size_t i=0;i<front.size();++i){const auto& triangle=topology.triangles[i];front[i]=orientation*triangle.normal.dot(g_cameraOrtho?localOutward:localEye-triangle.center)>=0;}
    for(const auto& edge:topology.edges){bool first=false,second=false;for(auto face:edge.faces){first|=front[face];second|=!front[face];}
        if(edge.faces.size()==1||(first&&second))for(auto point:{edge.a,edge.b}){auto projected=world.transformPoint(point);cached.boundary.insert(cached.boundary.end(),{(float)projected.x,(float)projected.y,(float)projected.z});}}
    ++g_bodyGlowBoundaryBuilds;return cached.boundary;
}
