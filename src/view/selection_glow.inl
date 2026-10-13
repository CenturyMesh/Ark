// Named-face boundary evidence comes from that face's tessellation only.
// Counting welded geometric edges removes interior triangulation diagonals.
static std::vector<float> g_selectionFaceBoundary;
static void appendSelectedFaceBoundary(const Mesh& mesh,int first,int count,const Mat4& world){
    using Point=std::array<float,3>;using Edge=std::pair<Point,Point>;
    std::map<Edge,unsigned> edges;
    for(int triangle=std::max(first,0);triangle<first+count;++triangle){
        Point points[3];bool valid=true;
        for(int corner=0;corner<3;++corner){size_t slot=(size_t)triangle*3+corner;if(slot>=mesh.idx.size()){valid=false;break;}size_t at=(size_t)mesh.idx[slot]*3;if(at+2>=mesh.pos.size()){valid=false;break;}points[corner]={mesh.pos[at],mesh.pos[at+1],mesh.pos[at+2]};}
        if(!valid)continue;for(int corner=0;corner<3;++corner){auto a=points[corner],b=points[(corner+1)%3];if(b<a)std::swap(a,b);++edges[{a,b}];}
    }
    for(const auto& edge:edges)if(edge.second==1)for(const auto& p:{edge.first.first,edge.first.second}){auto point=world.transformPoint({p[0],p[1],p[2]});g_selectionFaceBoundary.insert(g_selectionFaceBoundary.end(),{(float)point.x,(float)point.y,(float)point.z});}
}
