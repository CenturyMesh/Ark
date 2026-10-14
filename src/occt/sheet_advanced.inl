// Included in sheet_model.cpp's private namespace. All source recipes retain
// their authored expressions; only this disposable evaluation request expands.
struct MiterSource {std::string id,parent;std::vector<std::string> edges,lips;double gap=0,extension=0;};
struct AdvancedSheetSource {V records=V::makeArr();std::vector<MiterSource> miters;size_t sweptCount=0;};
void sheetFields(const V& row,std::initializer_list<const char*> fields){
    if(row.t!=V::Obj)throw std::runtime_error("Advanced sheet recipe must be an object");
    std::set<std::string> allowed;for(auto field:fields)allowed.insert(field);
    for(auto& entry:row.obj)if(!allowed.count(entry.first))throw std::runtime_error("Unsupported advanced sheet field: "+entry.first);
}
const std::vector<V>& sheetRows(const V& request,const char* key,size_t cap=32){
    static const std::vector<V> empty;auto rows=request.find(key);if(!rows)return empty;
    if(rows->t!=V::Arr||rows->arr.size()>cap)throw std::runtime_error(std::string("Expected at most ")+std::to_string(cap)+" "+key);
    return rows->arr;
}
bool sheetEdge(const std::string& edge){return edge=="u_min"||edge=="u_max"||edge=="v_min"||edge=="v_max";}
bool adjacentSheetEdges(const std::string& a,const std::string& b){return a[0]!=b[0];}
V advancedReceipt(const V& row,const char* kind){V out=V::makeObj();out.set("id",required(row,"id"));out.set("kind",V::makeStr(kind));out.set("manufacturing_qualified",V::makeBool(false));return out;}
void validateAuthoredBendTable(const V& request,const ExprEnv& env){
    auto table=request.find("bend_table");if(!table)return;sheetFields(*table,{"entries","interpolation","name"});
    if(table->find("name"))named(*table,"name");
    if(table->find("interpolation")&&string(*table,"interpolation")!="exact")throw std::runtime_error("Bend table supports exact keys only; interpolation and extrapolation are unsupported");
    const auto& entries=required(*table,"entries");if(entries.t!=V::Arr||entries.arr.empty()||entries.arr.size()>128)throw std::runtime_error("Bend table needs 1..128 explicit allowance entries");
    std::vector<std::array<double,3>> keys;for(auto& entry:entries.arr){sheetFields(entry,{"thickness","inner_radius","angle","bend_allowance"});const double t=expr(entry,"thickness",env),r=expr(entry,"inner_radius",env),angle=expr(entry,"angle",env),ba=expr(entry,"bend_allowance",env);
        if(!(t>0&&r>0&&angle>0&&angle<=180&&ba>0))throw std::runtime_error("Bend table entries need positive thickness, radius, unsigned angle and allowance");const double k=(ba/(angle*pi/180)-r)/t;if(k<0||k>1)throw std::runtime_error("Bend table allowance implies K factor outside [0,1]");
        for(auto& key:keys)if(std::abs(key[0]-t)<=1e-9&&std::abs(key[1]-r)<=1e-9&&std::abs(key[2]-angle)<=1e-9)throw std::runtime_error("Duplicate exact bend table key");keys.push_back({t,r,angle});
    }
}
AdvancedSheetSource expandAdvancedSheet(V& request,const ExprEnv& env,const Panel& root,double t,double /*defaultK*/){
    validateAuthoredBendTable(request,env);
    AdvancedSheetSource out;V flanges=request.find("flanges")?*request.find("flanges"):V::makeArr();
    if(flanges.t!=V::Arr)throw std::runtime_error("Sheet flanges must be an array");
    auto profile=[&](const V& row,const std::string& identity,const std::string& parent,const std::string& edge,double width,double offset){
        const auto& sections=required(row,"profile");if(sections.t!=V::Arr||sections.arr.empty()||sections.arr.size()>8)throw std::runtime_error("Contour profile needs 1..8 bend/straight segments");
        std::string previous=parent;for(size_t i=0;i<sections.arr.size();++i){const auto& section=sections.arr[i];sheetFields(section,{"length","angle","inner_radius","k_factor"});V child=section;
            const std::string id=identity+"."+std::to_string(i+1);child.set("id",V::makeStr(id));child.set("parent",V::makeStr(previous));child.set("edge",V::makeStr(i?"u_max":edge));
            if(!i){child.set("width",V::makeNum(width));child.set("offset",V::makeNum(offset));}
            flanges.arr.push_back(child);previous=id;}
        return previous;
    };
    for(auto& row:sheetRows(request,"swept_flanges",16)){
        sheetFields(row,{"id","parent","edge","profile","width","offset","path"});const auto id=named(row,"id"),parent=named(row,"parent"),edge=string(row,"edge");
        if(!sheetEdge(edge))throw std::runtime_error("Straight swept flange path must be a named straight panel edge");
        if(row.find("path")&&string(row,"path")!="straight_edge")throw std::runtime_error("Swept flange supports a constant contour along straight_edge only; curved/nondevelopable paths require a separate development model");
        // Width/offset expressions need the producer parent, so forward them
        // intact instead of evaluating in a guessed global coordinate frame.
        const size_t start=flanges.arr.size();profile(row,id,parent,edge,1,0);
        auto& first=flanges.arr[start];first.set("width",row.find("width")?*row.find("width"):V::makeStr("edge_length"));first.set("offset",row.find("offset")?*row.find("offset"):V::makeNum(0));
        auto receipt=advancedReceipt(row,"swept_flange");receipt.set("path_domain",V::makeStr("constant cross-section swept along a straight named edge"));receipt.set("profile_segment_count",V::makeNum(required(row,"profile").arr.size()));out.records.arr.push_back(receipt);++out.sweptCount;
    }
    for(auto& row:sheetRows(request,"miters",8)){
        sheetFields(row,{"id","parent","edges","profile","gap"});MiterSource m;m.id=named(row,"id");m.parent=named(row,"parent");m.gap=expr(row,"gap",env);
        if(m.parent!=root.id||!(m.gap>linearTol&&m.gap<=1000))throw std::runtime_error("Miter flange needs the original rectangular base and an explicit positive gap");
        const auto& selected=required(row,"edges");if(selected.t!=V::Arr||selected.arr.size()<2||selected.arr.size()>4)throw std::runtime_error("Miter flange needs 2..4 base edges");
        std::set<std::string> unique;for(auto& edge:selected.arr){if(edge.t!=V::Str||!sheetEdge(edge.str)||!unique.insert(edge.str).second)throw std::runtime_error("Miter edges must be unique u_min/u_max/v_min/v_max names");m.edges.push_back(edge.str);}
        bool corner=false;for(auto& a:m.edges)for(auto& b:m.edges)corner|=adjacentSheetEdges(a,b);if(!corner)throw std::runtime_error("Miter flange needs at least one adjacent edge pair");
        const auto& sections=required(row,"profile");if(sections.t!=V::Arr||sections.arr.size()!=2)throw std::runtime_error("Miter domain is a common wall and outward lip profile with two opposite 90 degree bends");
        const double a=expr(sections.arr[0],"angle",env),b=expr(sections.arr[1],"angle",env);
        if(std::abs(std::abs(a)-90)>1e-9||std::abs(a+b)>1e-9)throw std::runtime_error("Miter domain needs +90/-90 or -90/+90 wall and outward lip bends");
        const double r1=expr(sections.arr[0],"inner_radius",env),r2=expr(sections.arr[1],"inner_radius",env),lip=expr(sections.arr[1],"length",env);
        if(!(r1>0&&r2>0&&lip>0))throw std::runtime_error("Miter radii and lip length must be positive");
        const double cutback=r1+t+m.gap;m.extension=r1+r2+t+lip+2*cutback;
        for(auto& edge:m.edges){const double span=edge[0]=='u'?root.width:root.length;
            // edgeFrame's local span follows +v on u_max, -v on u_min,
            // -u on v_max and +u on v_min.
            const std::string start=edge=="u_max"?"v_min":edge=="u_min"?"v_max":edge=="v_max"?"u_max":"u_min";
            const std::string end=edge=="u_max"?"v_max":edge=="u_min"?"v_min":edge=="v_max"?"u_min":"u_max";
            const double lo=unique.count(start)?cutback:0,hi=unique.count(end)?cutback:0;
            if(span-lo-hi<=linearTol)throw std::runtime_error("Miter corner cutbacks consume the attachment edge");
            m.lips.push_back(profile(row,m.id+"."+edge,m.parent,edge,span-lo-hi,lo));
        }
        auto receipt=advancedReceipt(row,"miter_flange");receipt.set("gap_mm",V::makeNum(m.gap));receipt.set("corner_attachment_cutback_mm",V::makeNum(cutback));receipt.set("seam_model",V::makeStr("exact coplanar outward lip bisectors with symmetric positive normal gap"));out.records.arr.push_back(receipt);out.miters.push_back(m);
    }
    request.set("flanges",flanges);return out;
}
double polygonArea(const std::vector<P2>& points){double twice=0;for(size_t i=0;i<points.size();++i){auto a=points[i],b=points[(i+1)%points.size()];twice+=a.x*b.y-b.x*a.y;}return std::abs(twice)/2;}
TopoDS_Shape sheetPolygon(const std::vector<P2>& points,double t,double y=0){
    if(points.size()<3||polygonArea(points)<=linearTol*linearTol)throw std::runtime_error("Sheet operation leaves no panel area");
    BRepBuilderAPI_MakePolygon polygon;for(auto p:points)polygon.Add(gp_Pnt(p.x,y,p.y));polygon.Close();
    auto face=BRepBuilderAPI_MakeFace(polygon.Wire()).Face();auto shape=BRepPrimAPI_MakePrism(face,gp_Vec(0,t,0)).Shape();
    if(!BRepCheck_Analyzer(shape).IsValid()||shapes(shape,TopAbs_SOLID).size()!=1)throw std::runtime_error("Sheet polygon did not create one valid constant thickness solid");return shape;
}
std::vector<P2> clipSheetPolygon(const std::vector<P2>& source,double a,double b,double c){
    std::vector<P2> out;for(size_t i=0;i<source.size();++i){auto x=source[i],y=source[(i+1)%source.size()];double dx=a*x.x+b*x.y-c,dy=a*y.x+b*y.y-c;bool ix=dx<=linearTol,iy=dy<=linearTol;
        if(ix)out.push_back(x);if(ix!=iy){const double f=dx/(dx-dy);out.push_back({x.x+(y.x-x.x)*f,x.y+(y.y-x.y)*f});}}
    return out;
}
Panel& sheetPanel(std::vector<Panel>& panels,const std::string& id){for(auto& p:panels)if(p.id==id)return p;throw std::runtime_error("Advanced sheet operation references missing panel: "+id);}
void applyMiterOutlines(std::vector<Panel>& panels,const AdvancedSheetSource& source,double /*thickness*/){
    for(auto& m:source.miters){auto& root=sheetPanel(panels,m.parent);const auto inverse=root.formed.Inverted();
        for(size_t i=0;i<m.edges.size();++i){auto& p=sheetPanel(panels,m.lips[i]);p.outline={{0,-m.extension},{p.length,-m.extension},{p.length,p.width+m.extension},{0,p.width+m.extension}};
            const auto edge=m.edges[i];bool startTrim=false,endTrim=false;
            for(auto& other:m.edges){if(!adjacentSheetEdges(edge,other))continue;
                const std::string ue=edge[0]=='u'?edge:other,ve=edge[0]=='v'?edge:other;const double sx=ue=="u_max"?1:-1,sz=ve=="v_max"?1:-1;
                const double cx=sx>0?root.length:0,cz=sz>0?root.width:0;
                // Keep this edge's side of the corner bisector. Both lips
                // have equal signed elevation, and the plane is normal to
                // their skins, so these cuts preserve full wall thickness.
                double nx=-sx,nz=sz,c=-sx*cx+sz*cz-m.gap/std::sqrt(2.);
                if(edge[0]=='v'){nx=sx;nz=-sz;c=sx*cx-sz*cz-m.gap/std::sqrt(2.);}
                const auto frame=inverse*p.formed;const auto origin=gp_Pnt(0,0,0).Transformed(frame),u=gp_Pnt(1,0,0).Transformed(frame),v=gp_Pnt(0,0,1).Transformed(frame),normal=gp_Pnt(0,1,0).Transformed(frame);
                if(std::abs(nx*(normal.X()-origin.X())+nz*(normal.Z()-origin.Z()))>1e-8)throw std::runtime_error("Miter lip cut would bevel sheet thickness");
                p.outline=clipSheetPolygon(p.outline,nx*(u.X()-origin.X())+nz*(u.Z()-origin.Z()),nx*(v.X()-origin.X())+nz*(v.Z()-origin.Z()),c-nx*origin.X()-nz*origin.Z());
                const std::string start=edge=="u_max"?"v_min":edge=="u_min"?"v_max":edge=="v_max"?"u_max":"u_min";if(other==start)startTrim=true;else endTrim=true;
            }
            if(!startTrim)p.outline=clipSheetPolygon(p.outline,0,-1,0);
            if(!endTrim)p.outline=clipSheetPolygon(p.outline,0,1,p.width);
            if(polygonArea(p.outline)<=linearTol*linearTol)throw std::runtime_error("Miter gap consumes a lip");
        }
    }
}
void applyBendTable(const V& request,const V& row,Panel& p,double thickness,const ExprEnv& env){
    auto table=request.find("bend_table");if(!table)return;sheetFields(*table,{"entries","interpolation","name"});
    if(table->find("interpolation")&&string(*table,"interpolation")!="exact")throw std::runtime_error("Bend table supports exact keys only; interpolation and extrapolation are unsupported");
    const auto& entries=required(*table,"entries");if(entries.t!=V::Arr||entries.arr.empty()||entries.arr.size()>128)throw std::runtime_error("Bend table needs 1..128 explicit allowance entries");
    int matches=0;double allowance=0;for(auto& entry:entries.arr){sheetFields(entry,{"thickness","inner_radius","angle","bend_allowance"});double t=expr(entry,"thickness",env),r=expr(entry,"inner_radius",env),angle=expr(entry,"angle",env),ba=expr(entry,"bend_allowance",env);
        if(!(t>0&&r>0&&angle>0&&angle<=180&&ba>0))throw std::runtime_error("Bend table entries need positive thickness, radius, unsigned angle and allowance");
        const double k=(ba/(angle*pi/180)-r)/t;if(k<0||k>1)throw std::runtime_error("Bend table allowance implies K factor outside [0,1]");
        if(std::abs(t-thickness)<=1e-9&&std::abs(r-p.radius)<=1e-9&&std::abs(angle-std::abs(p.angle)*180/pi)<=1e-9){++matches;allowance=ba;}}
    if(matches!=1)throw std::runtime_error(matches?"Duplicate exact bend table key":"No exact bend table row for authored thickness, radius and angle");
    const double k=(allowance/std::abs(p.angle)-p.radius)/thickness;if(row.find("k_factor")&&std::abs(k-p.k)>1e-9)throw std::runtime_error("Explicit flange K factor conflicts with the authored bend table allowance");p.k=k;p.allowance=allowance;
}
void registerAdvancedIds(const V& request,std::set<std::string>& ids){for(auto key:{"miters","swept_flanges","corners","rips","tabs"})for(auto& row:sheetRows(request,key))if(!ids.insert(named(row,"id")).second)throw std::runtime_error("Duplicate advanced sheet source identity");}
std::vector<P2> sheetRect(double x0,double z0,double x1,double z1){return {{x0,z0},{x1,z0},{x1,z1},{x0,z1}};}
double addPanelMaterial(Panel& p,const TopoDS_Shape& material,double area,double t){
    if(properties(common(p.local,material))>std::max(1e-7,area*t*1e-9))throw std::runtime_error("Sheet addition overlaps existing authored material");
    const auto candidate=fuse(p.local,material);if(!BRepCheck_Analyzer(candidate).IsValid()||shapes(candidate,TopAbs_SOLID).size()!=1)throw std::runtime_error("Sheet addition is disconnected or invalid");p.local=candidate;p.area+=area;return area*t;
}
double removePanelMaterial(Panel& p,const TopoDS_Shape& tool,double area,double t){
    const double expected=area*t,actual=properties(common(p.local,tool));if(std::abs(actual-expected)>std::max(1e-6,expected*1e-8))throw std::runtime_error("Sheet edge treatment intersects another cut, attachment outline or insufficient material");
    const auto candidate=cut(p.local,tool);if(!BRepCheck_Analyzer(candidate).IsValid()||shapes(candidate,TopAbs_SOLID).size()!=1)throw std::runtime_error("Sheet edge treatment splits or removes the panel");p.local=candidate;p.area-=area;return expected;
}
void applyAdvancedPanel(const V& request,Panel& p,std::vector<Panel>& panels,double t,const ExprEnv& env,V& records){
    auto receipt=[&](const V& row,const char* kind,double delta){auto r=advancedReceipt(row,kind);r.set("panel",V::makeStr(p.id));r.set("analytic_volume_delta_mm3",V::makeNum(delta));r.set("constant_thickness",V::makeBool(true));records.arr.push_back(r);};
    for(auto& row:sheetRows(request,"corners")){const auto kind=string(row,"kind");const bool counterpart=kind=="closure"&&named(row,"target")==p.id;if(named(row,"panel")!=p.id&&!counterpart)continue;if(p.hem)throw std::runtime_error("Advanced edge treatments on open round hem returns are unsupported");
        if(kind=="closure"){
            sheetFields(row,{"id","kind","panel","target","side","gap","from"});auto& target=sheetPanel(panels,named(row,counterpart?"panel":"target"));if(&target==&p||p.parent<0||target.parent<0)throw std::runtime_error("Corner closure needs two perpendicular terminal flanges");
            for(auto& other:panels)if(other.parent==(int)(&p-panels.data()))throw std::runtime_error("Corner closure needs a terminal panel");
            ExprEnv sourceEnv=env;if(counterpart){sourceEnv.vars["panel_length"]=target.length;sourceEnv.vars["panel_width"]=target.width;}
            const double gap=expr(row,"gap",sourceEnv),from=expr(row,"from",sourceEnv);auto side=string(row,"side");if(!(gap>linearTol&&from>linearTol&&from<(counterpart?target.length:p.length)&& (side=="start"||side=="end")))throw std::runtime_error("Closure needs positive gap, interior from station and start/end side");
            const auto relative=p.formed.Inverted()*target.formed;const auto origin=gp_Pnt(0,0,0).Transformed(relative);const auto normal=gp_Vec(0,1,0).Transformed(relative);
            if(std::abs(normal.Z())<1-1e-9||std::abs(normal.X())>1e-9||std::abs(normal.Y())>1e-9)throw std::runtime_error("Closure target must be perpendicular with its normal aligned to the authored side span");
            const double z0=origin.Z(),z1=z0+normal.Z()*t;double end=0,begin=from,finish=p.length;
            if(counterpart){const auto along=gp_Vec(1,0,0).Transformed(relative);if(std::abs(along.X()-1)>1e-9)throw std::runtime_error("Matched corner extension needs parallel wall tangents");
                const double lower=std::min(z0,z1),upper=std::max(z0,z1);if(upper<-linearTol){side="start";end=lower;}else if(lower>p.width+linearTol){side="end";end=upper;}else throw std::runtime_error("Matched corner target is not beyond a terminal wall side");begin=std::max(linearTol,origin.X()+from);finish=std::min(p.length,origin.X()+target.length);if(finish-begin<=linearTol)throw std::runtime_error("Corner match has no overlapping finite wall height");}
            else {const double face=side=="end"?std::min(z0,z1):std::max(z0,z1);end=side=="end"?face-gap:face+gap;}
            const double extension=side=="end"?end-p.width:-end;
            if(!(extension>linearTol&&extension<=10000))throw std::runtime_error("Closure target does not permit a positive side extension with the declared gap");
            const double lo=side=="end"?p.width:end,hi=side=="end"?end:0;auto material=sheetPolygon(sheetRect(begin,lo,finish,hi),t);
            const double delta=addPanelMaterial(p,material,(finish-begin)*extension,t);receipt(row,counterpart?"corner_closure_match":"corner_closure",delta);records.arr.back().set("gap_mm",V::makeNum(gap));records.arr.back().set("extension_mm",V::makeNum(extension));records.arr.back().set("weld_material_added",V::makeBool(false));continue;
        }
        sheetFields(row,{"id","kind","panel","corner","shape","width","height","radius","distance"});if(kind!="relief"&&kind!="trim"&&kind!="break")throw std::runtime_error("Supported corner kinds are relief, trim, break and closure");
        const double which=expr(row,"corner",env);if(which<0||which>3||which!=std::floor(which))throw std::runtime_error("Corner must be 0..3 in local (u,v) order");
        const bool xmax=which==1||which==2,zmax=which==2||which==3;const double x=xmax?p.length:0,z=zmax?p.width:0,sx=xmax?-1:1,sz=zmax?-1:1;TopoDS_Shape tool;double area=0;
        if(kind=="break") {const double d=expr(row,"distance",env);if(!(d>linearTol&&d<p.length&&d<p.width))throw std::runtime_error("Corner break distance must lie inside panel extents");tool=sheetPolygon({{x,z},{x+sx*d,z},{x,z+sz*d}},t+2,-1);area=d*d/2;}
        else {const auto shape=row.find("shape")?string(row,"shape"):"rectangular";if(shape=="round"){const double radius=expr(row,"radius",env);if(!(radius>linearTol&&radius<p.length&&radius<p.width))throw std::runtime_error("Round relief radius must lie inside panel extents");tool=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,-1,z),gp_Dir(0,1,0)),radius,t+2).Shape();area=pi*radius*radius/4;}
            else if(shape=="rectangular"){const double w=expr(row,"width",env),h=expr(row,"height",env);if(!(w>linearTol&&h>linearTol&&w<p.length&&h<p.width))throw std::runtime_error("Corner trim/relief dimensions must lie inside panel extents");tool=sheetPolygon(sheetRect(std::min(x,x+sx*w),std::min(z,z+sz*h),std::max(x,x+sx*w),std::max(z,z+sz*h)),t+2,-1);area=w*h;}
            else throw std::runtime_error("Corner relief shape must be rectangular or round");}
        receipt(row,kind.c_str(),-removePanelMaterial(p,tool,area,t));
    }
    for(auto& row:sheetRows(request,"rips")){if(named(row,"panel")!=p.id)continue;sheetFields(row,{"id","panel","edge","station","width","depth"});const auto edge=string(row,"edge");if(!sheetEdge(edge)||p.hem)throw std::runtime_error("Rip needs a straight ordinary panel edge");
        const double station=expr(row,"station",env),w=expr(row,"width",env),d=expr(row,"depth",env),span=edge[0]=='u'?p.width:p.length,extent=edge[0]=='u'?p.length:p.width;
        if(!(station>linearTol&&w>linearTol&&station+w<span-linearTol&&d>linearTol&&d<extent-linearTol))throw std::runtime_error("Rip needs an interior edge station/width and a partial inward depth; splitting closed shells is unsupported");
        std::vector<P2> rectangle=edge=="u_max"?sheetRect(p.length-d,station,p.length,station+w):edge=="u_min"?sheetRect(0,station,d,station+w):edge=="v_max"?sheetRect(station,p.width-d,station+w,p.width):sheetRect(station,0,station+w,d);
        receipt(row,"rip",-removePanelMaterial(p,sheetPolygon(rectangle,t+2,-1),w*d,t));
    }
    for(auto& row:sheetRows(request,"tabs")){
        sheetFields(row,{"id","panel","edge","station","width","length","slot_panel","slot_x","slot_y","clearance"});const auto owner=named(row,"panel"),slot=named(row,"slot_panel");if(owner==slot)throw std::runtime_error("Tab and paired slot require different authored panels");
        auto& sourcePanel=sheetPanel(panels,owner);ExprEnv sourceEnv=env;sourceEnv.vars["panel_length"]=sourcePanel.length;sourceEnv.vars["panel_width"]=sourcePanel.width;
        const double w=expr(row,"width",sourceEnv),length=expr(row,"length",sourceEnv),clearance=expr(row,"clearance",sourceEnv);if(!(w>linearTol&&length>linearTol&&clearance>linearTol))throw std::runtime_error("Tab dimensions and explicit clearance must be positive");
        if(owner==p.id){const auto edge=string(row,"edge");if(!sheetEdge(edge))throw std::runtime_error("Tab edge must be u_min/u_max/v_min/v_max");const double station=expr(row,"station",env),span=edge[0]=='u'?p.width:p.length;if(station<=linearTol||station+w>=span-linearTol||p.hem)throw std::runtime_error("Tab must lie strictly inside a terminal straight panel edge");
            for(auto& other:panels)if(other.parent==(int)(&p-panels.data())&&other.edge==edge)throw std::runtime_error("Tab edge already has a forming attachment");
            auto rectangle=edge=="u_max"?sheetRect(p.length,station,p.length+length,station+w):edge=="u_min"?sheetRect(-length,station,0,station+w):edge=="v_max"?sheetRect(station,p.width,station+w,p.width+length):sheetRect(station,-length,station+w,0);
            receipt(row,"tab",addPanelMaterial(p,sheetPolygon(rectangle,t),w*length,t));records.arr.back().set("clearance_mm",V::makeNum(clearance));records.arr.back().set("assembly_fit_verified",V::makeBool(false));}
        if(slot==p.id){const double x=expr(row,"slot_x",env),z=expr(row,"slot_y",env),sw=w+2*clearance,sh=t+2*clearance;if(!(x-sw/2>linearTol&&x+sw/2<p.length-linearTol&&z-sh/2>linearTol&&z+sh/2<p.width-linearTol))throw std::runtime_error("Paired tab slot must lie strictly inside its target panel");
            auto tool=sheetPolygon(sheetRect(x-sw/2,z-sh/2,x+sw/2,z+sh/2),t+2,-1);receipt(row,"tab_slot",-removePanelMaterial(p,tool,sw*sh,t));records.arr.back().set("clearance_mm",V::makeNum(clearance));records.arr.back().set("assembly_fit_verified",V::makeBool(false));}
    }
}
void validateAdvancedBindings(const V& request,std::vector<Panel>& panels,V& records){
    for(auto key:{"corners","rips","tabs"})for(auto& row:sheetRows(request,key)){sheetPanel(panels,named(row,"panel"));if(std::string(key)=="tabs")sheetPanel(panels,named(row,"slot_panel"));}
    for(auto& row:sheetRows(request,"corners"))if(string(row,"kind")=="closure"){
        auto& p=sheetPanel(panels,named(row,"panel"));auto& target=sheetPanel(panels,named(row,"target"));BRepExtrema_DistShapeShape distance(transformed(p.formedLocal,p.formed),transformed(target.formedLocal,target.formed));
        auto receipt=std::find_if(records.arr.begin(),records.arr.end(),[&](const V& record){return string(record,"id")==named(row,"id")&&string(record,"kind")=="corner_closure";});if(receipt==records.arr.end()||!distance.IsDone()||std::abs(distance.Value()-number(*receipt,"gap_mm"))>linearTol)throw std::runtime_error("Final corner treatments do not preserve the declared exact closure gap");for(auto& record:records.arr)if(string(record,"id")==named(row,"id"))record.set("measured_gap_mm",V::makeNum(distance.Value()));
    }
    for(auto& row:sheetRows(request,"miters")){
        const auto& edges=required(row,"edges");double minimum=1e100;for(size_t i=0;i<edges.arr.size();++i)for(size_t j=i+1;j<edges.arr.size();++j){const auto a=edges.arr[i].str,b=edges.arr[j].str;if(!adjacentSheetEdges(a,b))continue;auto& pa=sheetPanel(panels,named(row,"id")+"."+a+".2");auto& pb=sheetPanel(panels,named(row,"id")+"."+b+".2");BRepExtrema_DistShapeShape distance(transformed(pa.formedLocal,pa.formed),transformed(pb.formedLocal,pb.formed));if(!distance.IsDone())throw std::runtime_error("Exact miter seam measurement failed");minimum=std::min(minimum,distance.Value());}
        auto receipt=std::find_if(records.arr.begin(),records.arr.end(),[&](const V& record){return string(record,"id")==named(row,"id");});if(receipt==records.arr.end()||minimum<number(*receipt,"gap_mm")-linearTol)throw std::runtime_error("Final miter lip clearance violates its declared gap");receipt->set("measured_min_gap_mm",V::makeNum(minimum));
    }
}
