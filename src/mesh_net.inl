// Parametric tube network: triangle authority; no implicit B-rep conversion or cloth solver.
static Mesh authoredMeshNet(const Feature& feature,const ExprEnv& env){
    if(!feature.inputs.empty())throw std::runtime_error("mesh_net has no source input; use explicit placement for a standalone net");
    const std::set<std::string> allowed{"width","depth","rows","columns","radius","sag","weave","density"};
    for(const auto& p:feature.p)if(!allowed.count(p.first))throw std::runtime_error("Unknown mesh_net parameter: "+p.first);
    auto number=[&](const char* key,double fallback){auto p=feature.p.find(key);if(p==feature.p.end())return fallback;bool ok=false;const double value=evalExpr(p->second,env,&ok);if(!ok||!std::isfinite(value))throw std::runtime_error(std::string("Invalid mesh_net expression: ")+key);return value;};
    const double width=number("width",100),depth=number("depth",100),r=number("radius",.5),sag=number("sag",0),weave=number("weave",1),nr=number("rows",8),nc=number("columns",8);
    if(width<=0||depth<=0||width>1e5||depth>1e5||nr<2||nc<2||nr>64||nc>64||nr!=std::floor(nr)||nc!=std::floor(nc)||r<=0||r>=std::min(width/(nc-1),depth/(nr-1))*.24||sag<0||sag>std::max(width,depth)|| (weave!=0&&weave!=1))throw std::runtime_error("mesh_net: dimensions 0..100000, integer rows/columns 2..64, radius below 24% pitch, nonnegative bounded sag and weave 0/1 required");
    Mesh mesh;constexpr int sides=8;
    auto strand=[&](bool alongX,int index,int count,int crossCount){
        const int steps=(crossCount-1)*4;const u32 start=(u32)mesh.vertCount();
        auto center=[&](double t){const double u=alongX?t:index/(double)(count-1),v=alongX?index/(double)(count-1):t;
            const double crossing=weave?r*1.1*std::cos(3.141592653589793*(t*(crossCount-1)+index))*(alongX?1:-1):0;
            return Vec3{u*width,v*depth,-sag*16*u*(1-u)*v*(1-v)+crossing};};
        for(int step=0;step<=steps;++step){const double t=step/(double)steps;const auto c=center(t);const auto tangent=(center(std::min(1.,t+1e-5))-center(std::max(0.,t-1e-5))).normalized();
            const auto a=Vec3{0,0,1}.cross(tangent).normalized(),b=tangent.cross(a).normalized();
            for(int side=0;side<sides;++side){const double angle=side*6.283185307179586/sides;const auto p=c+(a*std::cos(angle)+b*std::sin(angle))*r;mesh.pos.insert(mesh.pos.end(),{(float)p.x,(float)p.y,(float)p.z});}
        }
        for(int step=0;step<steps;++step)for(int side=0;side<sides;++side){const u32 a=start+step*sides+side,b=start+step*sides+(side+1)%sides,c=a+sides,d=b+sides;mesh.idx.insert(mesh.idx.end(),{a,b,c,b,d,c});}
        for(int cap=0;cap<2;++cap){const auto c=center(cap);const u32 middle=(u32)mesh.vertCount();mesh.pos.insert(mesh.pos.end(),{(float)c.x,(float)c.y,(float)c.z});for(int side=0;side<sides;++side){const u32 a=start+(cap?steps*sides:0)+side,b=start+(cap?steps*sides:0)+(side+1)%sides;if(cap)mesh.idx.insert(mesh.idx.end(),{middle,a,b});else mesh.idx.insert(mesh.idx.end(),{middle,b,a});}}
    };
    for(int row=0;row<(int)nr;++row)strand(true,row,(int)nr,(int)nc);
    for(int col=0;col<(int)nc;++col)strand(false,col,(int)nc,(int)nr);
    mesh.computeNormals();return mesh;
}
