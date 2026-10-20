// Included inside ark::occt's anonymous namespace by surfacing.cpp, after
// collisionReport. 046B motion clearance: one solid pair at many rigid poses.
// Per pose it keeps the collision contract (exact distance; exact boolean
// common for contact), but the common only runs within the contact tolerance:
// separated solids have an empty common. Shapes are relocated, never copied.
V collisionFrames(const V& request){
    tolerance(request);
    for(const auto& field:request.obj)if(field.first!="op"&&field.first!="a_brep"&&field.first!="b_brep"&&field.first!="poses"&&field.first!="tolerance"&&field.first!="budget_ms")throw std::runtime_error("Unknown collision_frames field: "+field.first);
    auto input=[&](const char* key){const V& encoded=required(request,key);if(encoded.t!=V::Str||encoded.str.empty())throw std::runtime_error("Collision B-rep must be nonempty base64");TopoDS_Shape shape=readBrep(encoded.str);shapeReport(shape,true);const auto solids=shapes(shape,TopAbs_SOLID);if(shapes(shape,TopAbs_FACE).size()!=shapes(solids.front(),TopAbs_FACE).size())throw std::runtime_error("Collision refuses stray faces outside the input solid");return shape;};
    const TopoDS_Shape a=input("a_brep"),b=input("b_brep");
    const V& poses=required(request,"poses");if(poses.t!=V::Arr||poses.arr.size()>256)throw std::runtime_error("collision_frames accepts an array of at most 256 poses");
    double budget=10000;if(const V* value=request.find("budget_ms")){budget=number(*value,"budget_ms");if(budget<1||budget>20000)throw std::runtime_error("collision_frames budget_ms must be in [1,20000]");}
    auto place=[&](const TopoDS_Shape& shape,const V& frame){V holder=V::makeObj();holder.set("frame",frame);const auto m=frameOf(holder);gp_Trsf t;t.SetValues(m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]);return shape.Moved(TopLoc_Location(t));};
    auto box=[](const std::array<double,6>& b){V out=V::makeArr();for(double x:b)out.arr.push_back(V::makeNum(x));return out;};
    const auto started=std::chrono::steady_clock::now();V rows=V::makeArr();
    for(const auto& pose:poses.arr){
        if(pose.t!=V::Arr||pose.arr.size()!=2)throw std::runtime_error("collision_frames pose must be [a_transform,b_transform]");
        const TopoDS_Shape pa=place(a,pose.arr[0]),pb=place(b,pose.arr[1]);
        BRepExtrema_DistShapeShape distance(pa,pb);distance.Perform();
        if(!distance.IsDone()||distance.NbSolution()<1||!std::isfinite(distance.Value())||distance.Value()<0)throw std::runtime_error("Exact collision distance failed");
        const double gap=distance.Value();double volume=0;size_t count=0;V row=V::makeObj();
        if(gap<=kTolerance){
            const TopoDS_Shape overlap=common(pa,pb);if(!overlap.IsNull()&&!BRepCheck_Analyzer(overlap).IsValid())throw std::runtime_error("Exact collision common failed B-rep validation");
            for(const auto& solid:shapes(overlap,TopAbs_SOLID)){shapeReport(solid,true);volume+=properties(solid);++count;}
            if(!std::isfinite(volume)||volume<0)throw std::runtime_error("Exact collision volume is invalid");if(count)row.set("overlap_bounds_mm",box(bounds(overlap)));
        }
        const bool interference=volume>0;V points=V::makeArr();points.arr.push_back(vec(distance.PointOnShape1(1).XYZ()));points.arr.push_back(vec(distance.PointOnShape2(1).XYZ()));
        row.set("min_distance_mm",V::makeNum(gap));row.set("interference",V::makeBool(interference));row.set("touching",V::makeBool(!interference&&gap<=kTolerance));row.set("common_volume_mm3",V::makeNum(volume));row.set("common_solids",V::makeNum((double)count));row.set("nearest_points_mm",points);row.set("inner_solution",V::makeBool(distance.InnerSolution()));
        rows.arr.push_back(std::move(row));
        if(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()>=budget)break;
    }
    V result=V::makeObj();result.set("frames",rows);result.set("completed",V::makeNum((double)rows.arr.size()));result.set("a_bounds_mm",box(bounds(a)));result.set("b_bounds_mm",box(bounds(b)));
    result.set("contact_tolerance_mm",V::makeNum(kTolerance));result.set("authority",V::makeStr("exact_native_BRep_distance_and_boolean_common"));result.set("common_scope",V::makeStr("boolean common evaluated only where the exact distance is within the contact tolerance"));
    result.set("display_mesh_authoritative",V::makeBool(false));result.set("trajectory_checked",V::makeBool(false));
    result.set("a_brep_sha256",V::makeStr(sha256(base64Decode(required(request,"a_brep").str))));result.set("b_brep_sha256",V::makeStr(sha256(base64Decode(required(request,"b_brep").str))));return result;
}
