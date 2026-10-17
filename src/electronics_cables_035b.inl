// Included by electronics.cpp inside its private normalization namespace.
// These opt-in recipes never change the legacy rounded-polyline derivation.
bool cableMetadata035B(const V& spec){
    for(const char* role:{"start","end"})if(const auto* item=spec.find(role);item&&item->t==V::Obj)
        for(const char* key:{"connector","pin","net"})if(item->find(key))return true;
    return false;
}
V cableEndpoints035B(const V& spec,const ExprEnv& env){
    V rows=V::makeArr();
    for(const char* role:{"start","end"}){
        const V& authored=need(spec,role);std::string name;Vec3 p=endpoint(authored,env,name);
        V row=V::makeObj();row.set("role",V::makeStr(role));row.set("name",V::makeStr(name));row.set("point_mm",point(p));
        if(authored.t==V::Obj)for(const char* key:{"connector","pin","net"})if(authored.find(key))row.set(key,V::makeStr(word(authored,key)));
        rows.arr.push_back(row);
    }
    return rows;
}
V normalizeSplineCable035B(const V& spec,const ExprEnv& env){
    const auto* mode=spec.find("route_mode");
    if(!mode||mode->t!=V::Str||mode->str!="spline")throw std::runtime_error("CABLE_MODE_INVALID: new cable profiles and tangents require explicit route_mode:spline");
    if(const auto* automatic=spec.find("auto_route")){
        if(automatic->t!=V::Bool)throw std::runtime_error("auto_route must be boolean");
        if(automatic->b)throw std::runtime_error("CABLE_MODE_INVALID: spline uses authored anchors; auto_route is not supported");
    }
    auto bounded=[](Vec3 p){if(std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>10000)throw std::runtime_error("Cable coordinates must stay within 10 m");};
    std::string startName,endName;std::vector<Vec3> path{endpoint(need(spec,"start"),env,startName)};
    V waypoints=V::makeArr();
    if(const auto* source=spec.find("waypoints")){
        if(source->t!=V::Arr||source->arr.size()>32)throw std::runtime_error("Cable waypoint limit is 32");
        for(const auto& item:source->arr){std::string name;Vec3 p=endpoint(item,env,name);path.push_back(p);V row=V::makeObj();row.set("name",V::makeStr(name));row.set("point",point(p));waypoints.arr.push_back(row);}
    }
    path.push_back(endpoint(need(spec,"end"),env,endName));double chord=0;
    for(size_t i=0;i<path.size();++i){bounded(path[i]);for(size_t j=0;j<i;++j)if(length(path[i]-path[j])<1e-4)throw std::runtime_error("CABLE_PATH_DEGENERATE: spline anchors must be distinct by at least 0.0001 mm");if(i)chord+=length(path[i]-path[i-1]);}
    if(chord>100000)throw std::runtime_error("Cable route exceeds 100 m limit");
    V output=V::makeObj(),points=V::makeArr(),report=V::makeObj(),section=V::makeObj(),profile=V::makeObj();
    double extent=0;
    if(const auto* authored=spec.find("profile")){
        if(authored->t!=V::Obj||word(*authored,"type")!="ribbon")throw std::runtime_error("CABLE_PROFILE_INVALID: profile.type must be ribbon; omit profile for a round cable");
        for(const auto& field:authored->obj)if(field.first!="type"&&field.first!="width"&&field.first!="thickness"&&field.first!="up")throw std::runtime_error("CABLE_PROFILE_INVALID: unknown ribbon profile field "+field.first);
        if(spec.find("radius"))throw std::runtime_error("CABLE_PROFILE_INVALID: ribbon uses width and thickness, not radius");
        const double width=positive(*authored,env,"width",1000),thickness=positive(*authored,env,"thickness",100);
        if(thickness<1e-4||width<1e-4||thickness>width)throw std::runtime_error("CABLE_PROFILE_INVALID: ribbon requires 0.0001 <= thickness <= width mm");
        const Vec3 up=p3(need(*authored,"up"),env);if(length(up)<1e-8)throw std::runtime_error("CABLE_FRAME_INVALID: ribbon up must be nonzero");
        profile.set("type",V::makeStr("ribbon"));profile.set("width",V::makeNum(width));profile.set("thickness",V::makeNum(thickness));profile.set("up",point(up));
        section.set("type",V::makeStr("ribbon"));section.set("width_mm",V::makeNum(width));section.set("thickness_mm",V::makeNum(thickness));section.set("area_mm2",V::makeNum(width*thickness));extent=std::hypot(width,thickness)/2;
    }else{
        const double radius=positive(spec,env,"radius",100);if(radius<1e-4)throw std::runtime_error("CABLE_PROFILE_INVALID: spline radius must be at least 0.0001 mm");
        output.set("radius",V::makeNum(radius));profile.set("type",V::makeStr("round"));profile.set("radius",V::makeNum(radius));
        section.set("type",V::makeStr("round"));section.set("radius_mm",V::makeNum(radius));section.set("area_mm2",V::makeNum(std::acos(-1.)*radius*radius));extent=radius;
    }
    const double bend=get(spec,env,"bend_radius",extent*3);
    if(bend<=extent||bend>1e5)throw std::runtime_error("CABLE_BEND_INVALID: minimum centerline bend radius must exceed the profile circumradius and be bounded");
    const auto* first=spec.find("start_tangent"),*last=spec.find("end_tangent");
    if((first!=nullptr)!=(last!=nullptr))throw std::runtime_error("CABLE_FRAME_INVALID: start_tangent and end_tangent must be authored together");
    if(first){for(const char* key:{"start_tangent","end_tangent"}){const Vec3 tangent=p3(need(spec,key),env);if(length(tangent)<1e-8)throw std::runtime_error("CABLE_FRAME_INVALID: endpoint tangent must be nonzero");output.set(key,point(tangent));}}
    V obstacles=V::makeArr();
    if(const auto* source=spec.find("obstacles")){
        if(source->t!=V::Arr||source->arr.size()>64)throw std::runtime_error("Cable obstacle limit is 64");
        size_t index=0;for(const auto& item:source->arr){if(item.t!=V::Obj)throw std::runtime_error("Cable obstacle must be an object");const Vec3 lo=p3(need(item,"min"),env),hi=p3(need(item,"max"),env);bounded(lo);bounded(hi);if(hi.x<=lo.x||hi.y<=lo.y||hi.z<=lo.z)throw std::runtime_error("Cable obstacle bounds must increase");V row=V::makeObj();row.set("name",item.find("name")?V::makeStr(word(item,"name")):V::makeStr("obstacle."+std::to_string(index)));row.set("min",point(lo));row.set("max",point(hi));obstacles.arr.push_back(row);++index;}
    }
    for(Vec3 p:path)points.arr.push_back(point(p));
    output.set("kind",V::makeStr("cable_route"));output.set("route_mode",V::makeStr("spline"));output.set("path",points);output.set("profile",profile);output.set("bend_radius",V::makeNum(bend));output.set("obstacles",obstacles);output.set("waypoints",waypoints);
    output.set("start_name",V::makeStr(startName));output.set("end_name",V::makeStr(endName));
    report.set("route_mode",V::makeStr("spline"));report.set("path",points);report.set("start_name",V::makeStr(startName));report.set("end_name",V::makeStr(endName));report.set("endpoints",cableEndpoints035B(spec,env));report.set("cross_section",section);report.set("route_chord_length_mm",V::makeNum(chord));
    report.set("route_length_pending_native",V::makeBool(true));report.set("minimum_bend_radius_required_mm",V::makeNum(bend));report.set("bend_radius_checked",V::makeBool(false));report.set("self_intersections_checked",V::makeBool(false));report.set("obstacle_prefilter_checked",V::makeBool(false));report.set("exact_obstacle_checked",V::makeBool(false));report.set("auto_routed",V::makeBool(false));report.set("physical_mass_supported",V::makeBool(true));report.set("manufacturing_qualified",V::makeBool(false));
    report.set("route_scope",V::makeStr("authored interpolating 3D spline; native construction must check curvature, B-rep self-interference and declared AABB obstacles; no elastic, connector-fit, electrical, thermal or manufacturing qualification"));
    output.set("report",report);return output;
}
