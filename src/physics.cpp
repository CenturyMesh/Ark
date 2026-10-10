#include "ark/physics.hpp"
#include "ark/native_features.hpp"
#include <array>
#include <map>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;using Tensor=std::array<double,9>;
const V& need(const V& v,const char* key){auto p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing physics field: ")+key);return *p;}
double number(const V& v){if(v.t!=V::Num||!std::isfinite(v.num))throw std::runtime_error("Physics value must be finite");return v.num;}
Vec3 vector(const V& v){if(v.t!=V::Arr||v.arr.size()!=3)throw std::runtime_error("Physics vector needs three numbers");Vec3 p{number(v.arr[0]),number(v.arr[1]),number(v.arr[2])};if(std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1e12)throw std::runtime_error("Physics vector exceeds supported range");return p;}
V json(Vec3 p){V a=V::makeArr();for(double n:{p.x,p.y,p.z})a.arr.push_back(V::makeNum(n));return a;}
V json(const Tensor& p){V a=V::makeArr();for(double n:p)a.arr.push_back(V::makeNum(n));return a;}
Tensor tensor(const V& v){if(v.t!=V::Arr||v.arr.size()!=9)throw std::runtime_error("Inertia tensor needs nine entries");Tensor a{};for(size_t i=0;i<9;++i)a[i]=number(v.arr[i]);return a;}
Tensor rotate(const Tensor& a,const Mat4& frame){Tensor b{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)for(int l=0;l<3;++l)b[(size_t)i*3+j]+=frame.m[k*4+i]*a[(size_t)k*3+l]*frame.m[l*4+j];return b;}
Tensor shifted(Tensor a,double mass,Vec3 r){const double xyz[]{r.x,r.y,r.z};for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[(size_t)i*3+j]+=mass*((i==j?r.dot(r):0)-xyz[i]*xyz[j]);return a;}
Vec3 multiply(const Tensor& a,Vec3 v){return {a[0]*v.x+a[1]*v.y+a[2]*v.z,a[3]*v.x+a[4]*v.y+a[5]*v.z,a[6]*v.x+a[7]*v.y+a[8]*v.z};}
V accepted(V response){if(need(response,"isError").b)throw std::runtime_error(need(need(response,"structuredContent"),"error").str);return need(response,"structuredContent");}
u64 featureId(const Document& d,const V& value){u64 id=0;int count=0;for(const auto& f:d.features)if((value.t==V::Str&&f.name==value.str)||(value.t==V::Num&&value.num==(double)f.id)){id=f.id;++count;}if(count!=1)throw std::runtime_error("Physics feature must resolve uniquely");return id;}
struct Part {double kg=0;Vec3 center;Tensor inertia{};u64 feature=0,node=0;int copy=0;std::string name;};
double density(const Document& d,const Feature& feature){
    if(auto p=feature.p.find("density");p!=feature.p.end()){bool ok=false;double value=evalExpr(p->second,d.buildEnv(),&ok);if(!ok||value<=0||value>100)throw std::runtime_error("Invalid density override for "+feature.name);return value;}
    for(const auto& m:d.materials)if(m.name==feature.material&&m.densityGcm3>0)return m.densityGcm3;
    return 0;
}
Part localPart(AgentSession& session,u64 id){
    auto* feature=session.document.find(id);if(!feature)throw std::runtime_error("Physics feature not found");
    if(feature->type==FeatureType::PCB)throw std::runtime_error("PCB requires per-material substrate, copper and component masses; a uniform density is not authoritative");
    ensureNativeBody(session.document,id,session.document.evalBodies);const Body* body=nullptr;for(const auto& b:session.document.evalBodies)if(b.id==id)body=&b;
    Part part;part.feature=id;part.name=feature->name;
    if(feature->type==FeatureType::ConstructionFrame){if(body)if(const auto* report=body->nativeData.find("frame_report")){if(auto checked=report->find("mass_checked");checked&&checked->b&&report->find("inertia_g_mm2")){part.kg=number(need(*report,"mass_g"))/1000;part.center=vector(need(*report,"center_mm"));part.inertia=tensor(need(*report,"inertia_g_mm2"));for(auto& n:part.inertia)n*=1e-9;return part;}}throw std::runtime_error("Assign checked density to every frame profile");}
    double rho=density(session.document,*feature);
    if(!(rho>0))throw std::runtime_error("Assign a material density to "+feature->name);
    V args=V::makeObj(),queries=V::makeArr(),query=V::makeObj();args.set("id",V::makeNum((double)id));query.set("action",V::makeStr("properties"));query.set("density_g_cm3",V::makeNum(rho));queries.arr.push_back(query);args.set("queries",queries);
    const auto data=accepted(session.call("geometry_query",args));const auto& row=need(data,"results").arr[0];if(!need(row,"ok").b)throw std::runtime_error(need(row,"error").str);
    part.kg=number(need(row,"mass_g"))/1000;part.center=vector(need(row,"center_mm"));part.inertia=tensor(need(row,"mass_inertia_g_mm2_row_major"));for(auto& n:part.inertia)n*=1e-9;return part;
}
}
V inspectMaterials(const Document& document){
    V result=V::makeObj(),rows=V::makeArr();for(const auto& material:document.materials){V row=V::makeObj();row.set("name",V::makeStr(material.name));row.set("density_g_cm3",V::makeNum(material.densityGcm3));row.set("density_kg_m3",V::makeNum(material.densityGcm3*1000));row.set("density_assigned",V::makeBool(material.densityGcm3>0));row.set("physical",material.physical);row.set("appearance",appearanceToJson(material.appearance));V features=V::makeArr();for(const auto& f:document.features)if(f.material==material.name)features.arr.push_back(V::makeNum((double)f.id));row.set("features",features);rows.arr.push_back(row);}result.set("materials",rows);result.set("density_authority",V::makeStr("caller_declared_uniform_density_or_frame_profile"));return result;
}
V inspectPhysics(AgentSession& session,const V& args){
    const auto& doc=session.document;const Vec3 gravity=args.find("gravity")?vector(*args.find("gravity")):Vec3{0,0,-9.80665};if(gravity.len()>100)throw std::runtime_error("Gravity magnitude exceeds 100 m/s2");
    const Vec3 reference=args.find("reference_point_mm")?vector(*args.find("reference_point_mm")):Vec3{};
    V result=V::makeObj(),missing=V::makeArr(),rows=V::makeArr();std::vector<Part> parts;std::map<u64,Part> definitions;
    auto get=[&](u64 id)->Part{auto it=definitions.find(id);if(it==definitions.end())it=definitions.emplace(id,localPart(session,id)).first;return it->second;};
    const bool byFeature=args.find("id")!=nullptr;result.set("scope",V::makeStr(byFeature?"feature_local":doc.assembly.empty()?"unconsumed_features":"visible_assembly_instances"));
    if(byFeature){try{parts.push_back(get(featureId(doc,*args.find("id"))));}catch(const std::exception& e){missing.arr.push_back(V::makeStr(e.what()));}}
    else if(!doc.assembly.empty()){
        std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(doc,placements,error))throw std::runtime_error(error);
        for(const auto& placement:placements)if(placement.visible){try{Part p=get(placement.feature);p.node=placement.node;p.copy=placement.copy;p.name=placement.name;p.center=placement.world.transformPoint(p.center);p.inertia=rotate(p.inertia,placement.world);parts.push_back(p);}catch(const std::exception& e){missing.arr.push_back(V::makeStr(placement.name+": "+e.what()));}}
    }else{
        std::set<u64> consumed;for(const auto& f:doc.features)for(u64 id:f.inputs)consumed.insert(id);
        for(const auto& f:doc.features)if(!f.suppressed&&!consumed.count(f.id)){try{parts.push_back(get(f.id));}catch(const std::exception& e){missing.arr.push_back(V::makeStr(e.what()));}}
    }
    result.set("revision",V::makeNum((double)session.revision));result.set("gravity_vector_m_s2",json(gravity));result.set("gravity_m_s2",V::makeNum(gravity.len()));result.set("reference_point_mm",json(reference));result.set("mass_checked",V::makeBool(missing.arr.empty()&&!parts.empty()));result.set("missing",missing);
    if(!missing.arr.empty()||parts.empty()){result.set("mass_reason",V::makeStr(parts.empty()&&missing.arr.empty()?"No physical solids in scope":"Some bodies lack checked density or exact mass properties"));return result;}
    double mass=0;Vec3 weighted;for(const auto& p:parts){mass+=p.kg;weighted+=p.center*p.kg;}if(!(mass>0))throw std::runtime_error("Total mass is not positive");Vec3 center=weighted/mass;Tensor inertia{};
    for(const auto& p:parts){auto term=shifted(p.inertia,p.kg,(p.center-center)/1000);for(size_t i=0;i<9;++i)inertia[i]+=term[i];V row=V::makeObj();row.set("feature",V::makeNum((double)p.feature));row.set("node",V::makeNum((double)p.node));row.set("copy",V::makeNum(p.copy));row.set("name",V::makeStr(p.name));row.set("mass_kg",V::makeNum(p.kg));row.set("center_mm",json(p.center));rows.arr.push_back(row);}
    result.set("bodies",rows);result.set("mass_kg",V::makeNum(mass));result.set("mass_g",V::makeNum(mass*1000));result.set("center_mm",json(center));result.set("center_of_mass_mm",json(center));result.set("center_of_gravity_mm",gravity.len()>1e-12?json(center):V{});result.set("gravity_field",V::makeStr("uniform"));result.set("inertia_kg_m2",json(inertia));result.set("inertia_reference_kg_m2",json(shifted(inertia,mass,(center-reference)/1000)));result.set("inertia_convention",V::makeStr("row_major_centroid_tensor_in_report_axes"));
    if(byFeature){const auto* feature=session.document.find(parts.front().feature);double rho=feature&&feature->type!=FeatureType::ConstructionFrame?density(doc,*feature):0;if(rho>0){result.set("density_g_cm3",V::makeNum(rho));result.set("density_kg_m3",V::makeNum(rho*1000));}}
    Vec3 weight=gravity*mass,force=weight,torque=(center-reference).cross(weight)/1000;
    result.set("weight_N",V::makeNum(weight.len()));result.set("weight_vector_N",json(weight));result.set("gravity_moment_Nm",json(torque));
    if(const auto* loads=args.find("loads")){if(loads->t!=V::Arr||loads->arr.size()>64)throw std::runtime_error("Loads must be an array of up to 64 loads");for(const auto& load:loads->arr){Vec3 f=load.find("force_N")?vector(*load.find("force_N")):Vec3{};Vec3 p=load.find("point_mm")?vector(*load.find("point_mm")):center;Vec3 couple=load.find("torque_Nm")?vector(*load.find("torque_Nm")):Vec3{};force+=f;torque+=(p-reference).cross(f)/1000+couple;}}
    result.set("net_force_N",json(force));result.set("net_moment_at_reference_Nm",json(torque));result.set("support_reaction_force_N",json(-force));result.set("support_reaction_moment_Nm",json(-torque));result.set("reaction_scope",V::makeStr("static_balance_single_ideal_fixed_support_at_reference"));
    Vec3 velocity=args.find("velocity_m_s")?vector(*args.find("velocity_m_s")):Vec3{},omega=args.find("angular_velocity_rad_s")?vector(*args.find("angular_velocity_rad_s")):Vec3{};
    result.set("kinetic_energy_J",V::makeNum(.5*mass*velocity.dot(velocity)+.5*omega.dot(multiply(inertia,omega))));result.set("potential_energy_J",V::makeNum(-mass*gravity.dot((center-reference)/1000)));
    Vec3 acceleration=args.find("acceleration_m_s2")?vector(*args.find("acceleration_m_s2")):Vec3{},alpha=args.find("angular_acceleration_rad_s2")?vector(*args.find("angular_acceleration_rad_s2")):Vec3{};
    result.set("required_resultant_force_N",json(acceleration*mass));result.set("required_resultant_moment_at_com_Nm",json(multiply(inertia,alpha)+omega.cross(multiply(inertia,omega))));result.set("physics_model",V::makeStr("rigid_body_mass_properties_and_instantaneous_balance"));result.set("deformation_simulated",V::makeBool(false));result.set("collision_simulated",V::makeBool(false));
    result.set("linear_momentum_kg_m_s",json(velocity*mass));result.set("angular_momentum_at_com_kg_m2_s",json(multiply(inertia,omega)));
    result.set("additional_force_for_motion_N",json(acceleration*mass-force));Vec3 momentAtCom=torque-(center-reference).cross(force)/1000;
    result.set("additional_moment_for_motion_at_com_Nm",json(multiply(inertia,alpha)+omega.cross(multiply(inertia,omega))-momentAtCom));
    if(const auto* support=args.find("support")){
        Vec3 origin=vector(need(*support,"origin_mm")),normal=vector(need(*support,"normal"));if(normal.len()<1e-12)throw std::runtime_error("Support normal is zero");normal=normal/normal.len();double divisor=gravity.dot(normal);if(std::abs(divisor)<1e-12)throw std::runtime_error("Gravity does not intersect support plane");double travel=(origin-center).dot(normal)/divisor;if(travel<0)throw std::runtime_error("Support plane lies opposite the gravity direction");Vec3 projection=center+gravity*travel;
        const V& polygon=need(*support,"polygon_mm");if(polygon.t!=V::Arr||polygon.arr.size()<3||polygon.arr.size()>64)throw std::runtime_error("Support polygon needs 3..64 vertices");std::vector<Vec3> vertices;for(const auto& p:polygon.arr){Vec3 v=vector(p);if(std::abs((v-origin).dot(normal))>1e-6)throw std::runtime_error("Support polygon is not planar");vertices.push_back(v);}
        Vec3 x=vertices[1]-vertices[0];if(x.len()<1e-9)throw std::runtime_error("Support polygon has duplicate vertices");x=x/x.len();Vec3 y=normal.cross(x);bool inside=false;double margin=INFINITY,area=0;
        for(size_t i=0;i<vertices.size();++i)for(size_t j=i+1;j<vertices.size();++j){if(j==i+1||(i==0&&j+1==vertices.size()))continue;Vec3 a=vertices[i],b=vertices[(i+1)%vertices.size()],c=vertices[j],d=vertices[(j+1)%vertices.size()];auto side=[&](Vec3 p,Vec3 q,Vec3 r){return (q-p).cross(r-p).dot(normal);};double s1=side(a,b,c),s2=side(a,b,d),s3=side(c,d,a),s4=side(c,d,b);if(s1*s2<=0&&s3*s4<=0){auto overlap=[](double a,double b,double c,double d){return std::max(std::min(a,b),std::min(c,d))<=std::min(std::max(a,b),std::max(c,d))+1e-9;};if(overlap(a.dot(x),b.dot(x),c.dot(x),d.dot(x))&&overlap(a.dot(y),b.dot(y),c.dot(y),d.dot(y)))throw std::runtime_error("Support polygon self-intersects");}}
        for(size_t i=0;i<vertices.size();++i){Vec3 a=vertices[i]-projection,b=vertices[(i+1)%vertices.size()]-projection,d=b-a;double length=d.dot(d);if(length<1e-18)throw std::runtime_error("Support polygon has a degenerate edge");double t=std::clamp(-a.dot(d)/length,0.,1.);margin=std::min(margin,(a+d*t).len());double ax=a.dot(x),ay=a.dot(y),bx=b.dot(x),by=b.dot(y);area+=ax*by-bx*ay;if((ay>0)!=(by>0)&&0<(bx-ax)*(-ay)/(by-ay)+ax)inside=!inside;}
        if(std::abs(area)<1e-9)throw std::runtime_error("Support polygon has zero area");if(margin<1e-7)inside=true;
        result.set("gravity_projection_mm",json(projection));result.set("gravity_projection_inside_support",V::makeBool(inside));result.set("gravity_stability_margin_mm",V::makeNum(inside?margin:-margin));result.set("stability_scope",V::makeStr("gravity_only_declared_support_polygon_no_contact_solver"));
    }
    if(const auto* axial=args.find("axial_test")){
        const auto& name=need(*axial,"material");if(name.t!=V::Str)throw std::runtime_error("Axial material must be a name");const Material* material=nullptr;for(const auto& m:doc.materials)if(m.name==name.str)material=&m;if(!material)throw std::runtime_error("Axial material not found");double modulus=number(need(material->physical,"young_modulus_Pa"));double area=number(need(*axial,"area_mm2")),length=number(need(*axial,"length_mm")),load=number(need(*axial,"force_N"));if(modulus<=0||area<=0||length<=0)throw std::runtime_error("Axial test requires positive modulus, area and length");double stress=load/(area*1e-6),strain=stress/modulus;V axialResult=V::makeObj();axialResult.set("stress_Pa",V::makeNum(stress));axialResult.set("strain",V::makeNum(strain));axialResult.set("extension_mm",V::makeNum(strain*length));if(auto yield=material->physical.find("yield_strength_Pa"))axialResult.set("yield_ratio",stress!=0?V::makeNum(number(*yield)/std::abs(stress)):V{});axialResult.set("model",V::makeStr("uniform_uniaxial_small_strain_no_buckling"));result.set("axial_test",axialResult);
    }
    std::function<void(const V&)> finite=[&](const V& v){if(v.t==V::Num&&!std::isfinite(v.num))throw std::runtime_error("Physics calculation exceeded finite range");for(const auto& item:v.arr)finite(item);for(const auto& item:v.obj)finite(item.second);};finite(result);
    return result;
}
}
