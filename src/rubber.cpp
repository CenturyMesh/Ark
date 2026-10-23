#include "ark/rubber.hpp"
#include "ark/rubber_patterns.hpp"
#include "ark/rubber_dynamics.hpp"
#include "ark/native_features.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr double pi=3.14159265358979323846;
void need(bool condition,const std::string& reason){if(!condition)throw std::runtime_error("RUBBER: "+reason);}
const V& field(const V& v,const char* key){const auto* p=v.find(key);need(p!=nullptr,std::string("Missing ")+key);return *p;}
V json(const char* text){V v;need(arkjson::parse(text,v),"Internal schema parse failure");return v;}
std::string word(const V& v,const char* key,const char* fallback=""){const auto* p=v.find(key);if(!p)return fallback;need(p->t==V::Str&&!p->str.empty(),std::string("Expected text: ")+key);return p->str;}
double number(const V& v,const char* key,double fallback){const auto* p=v.find(key);if(!p)return fallback;need(p->t==V::Num&&std::isfinite(p->num),std::string("Expected finite number: ")+key);return p->num;}
unsigned integer(const V& v,const char* key,unsigned fallback,unsigned lo,unsigned hi){double n=number(v,key,fallback);need(n>=lo&&n<=hi&&n==std::floor(n),std::string("Invalid integer: ")+key);return (unsigned)n;}
bool boolean(const V& v,const char* key,bool fallback){const auto* p=v.find(key);if(!p)return fallback;need(p->t==V::Bool,std::string("Expected boolean: ")+key);return p->b;}
void keys(const V& v,std::initializer_list<const char*> list){need(v.t==V::Obj,"Expected object");std::set<std::string> allowed;for(auto key:list)allowed.insert(key);for(const auto& p:v.obj)need(allowed.count(p.first)!=0,"Unknown field: "+p.first);}
Vec3 vector(const V& a){need(a.t==V::Arr&&a.arr.size()==3,"Expected XYZ vector");for(const auto& n:a.arr)need(n.t==V::Num&&std::isfinite(n.num),"Nonfinite vector");return {a.arr[0].num,a.arr[1].num,a.arr[2].num};}
V vector(Vec3 p){V v=V::makeArr();for(auto n:{p.x,p.y,p.z})v.arr.push_back(V::makeNum(n));return v;}
void revision(AgentSession& s,const V& a){const auto& r=field(a,"expected_revision");need(r.t==V::Num&&r.num==(double)s.revision,"Expected revision does not match current drawing");}
const V& accepted(const V& r){if(const auto* e=r.find("isError");e&&e->b){const auto* d=r.find("structuredContent");throw std::runtime_error(d&&d->find("error")?d->find("error")->asStr():"Native query failed");}return field(r,"structuredContent");}
std::vector<std::string> dimensions(const std::string& kind){
    if(kind=="belt"||kind=="track")return {"center_distance_mm","radius_mm","thickness_mm","width_mm"};
    if(kind=="tire")return {"major_radius_mm","radial_radius_mm","axial_radius_mm","wall_mm"};
    if(kind=="ball")return {"radius_mm","wall_mm"};
    throw std::runtime_error("RUBBER: kind must be belt, track, tire or ball");
}
}
V rubberCapabilities(){V v=json(R"({"module":"rubber_059c","kinds":["belt","track","tire","ball"],"authority":"exact editable native_solid rubber_body; CAD dimensions in mm","geometry":{"belt_track":"equal-radius racetrack annulus; radius is pitch radius; width along Z","tire":"elliptical torus about Z; wall=0 solid, positive wall makes closed cavity","ball":"sphere; wall=0 solid, positive wall makes closed cavity","relief":"optional exact fused spike/lug/pebble; max 256 motifs; reject disconnected or invalid solids"},"appearance":"pebble, traction and chevron are procedural display patterns, not geometric relief","manufacturing_qualified":false})");v.set("patterns",rubberPatternCapabilities());v.set("dynamics",rubberDynamicsCapabilities());return v;}
V rubberPlanSchema(){V v=json(R"({"type":"object","properties":{"action":{"type":"string","enum":["capabilities","create","pattern"]},"expected_revision":{"type":"integer","minimum":0},"kind":{"type":"string","enum":["belt","track","tire","ball"]},"name":{"type":"string"},"dimensions":{"type":"object","additionalProperties":{"type":["number","string"]}},"material":{"type":"string"},"relief":{"type":"object"},"appearance":{"type":"object"},"recipe":{"type":"object"},"offset":{"type":"integer","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":256}},"additionalProperties":false})");auto p=*v.find("properties");p.set("recipe",rubberPatternSchema());v.set("properties",p);return v;}
V planRubber(AgentSession& s,const V& a){
    const auto action=word(a,"action","capabilities");
    if(action=="capabilities"){keys(a,{"action"});return rubberCapabilities();}
    if(action=="pattern"){keys(a,{"action","recipe","offset","limit"});return previewRubberPattern(field(a,"recipe"),integer(a,"offset",0,0,1000000),integer(a,"limit",64,1,256));}
    need(action=="create","Unknown action");keys(a,{"action","expected_revision","kind","name","dimensions","material","relief","appearance"});revision(s,a);
    const auto kind=word(a,"kind"),name=word(a,"name",("Rubber "+kind).c_str());need(name.size()<=128,"Name exceeds 128 bytes");const auto required=dimensions(kind);const auto& p=field(a,"dimensions");need(p.t==V::Obj,"Dimensions must be an object");
    for(const auto& k:required)need(p.find(k.c_str())!=nullptr,"Missing dimension: "+k);
    for(const auto& item:p.obj){need(std::find(required.begin(),required.end(),item.first)!=required.end(),"Unknown dimension: "+item.first);need((item.second.t==V::Num&&std::isfinite(item.second.num))||(item.second.t==V::Str&&!item.second.str.empty()&&item.second.str.size()<=1024),"Dimensions must be numbers or expressions");}
    V cmd=json(R"({"op":"feature.add","type":"native_solid","native":{"op":"rubber_body"}})");cmd.set("name",V::makeStr(name));cmd.set("p",p);auto native=*cmd.find("native");native.set("kind",V::makeStr(kind));if(const auto* relief=a.find("relief")){need(relief->t==V::Obj,"Relief must be an object");native.set("relief",*relief);}cmd.set("native",native);
    if(const auto* material=a.find("material")){need(material->t==V::Str&&!material->str.empty(),"Material must be a name");bool found=false;for(const auto& m:s.document.materials)found|=m.name==material->str;need(found,"Declare the material with material.set before assigning it");cmd.set("material",*material);}
    cmd.set("appearance",a.find("appearance")?*a.find("appearance"):json(R"({"base_color":[0.055,0.065,0.075],"roughness":0.82,"metallic":0})"));
    V commands=V::makeArr();commands.arr.push_back(cmd);V args=V::makeObj();args.set("expected_revision",V::makeNum((double)s.revision));args.set("commands",commands);args.set("detail",V::makeStr("changes"));V call=V::makeObj();call.set("tool",V::makeStr("doc_preview"));call.set("arguments",args);V out=V::makeObj();out.set("revision",V::makeNum((double)s.revision));out.set("commands",commands);out.set("preview",call);out.set("note",V::makeStr("Read-only plan. Preview evaluates expressions and validates exact B-rep before atomic publication; no material strength is inferred."));return out;
}
V rubberStudySchema(){return json(R"({"type":"object","properties":{"action":{"type":"string","enum":["capabilities","run"]},"id":{"type":"integer","minimum":1},"backend":{"type":"string","enum":["gpu","cpu"]},"duration_s":{"type":"number","exclusiveMinimum":0,"maximum":10},"step_s":{"type":"number"},"iterations":{"type":"integer","minimum":1,"maximum":32},"output_fps":{"type":"integer","minimum":1,"maximum":60},"around":{"type":"integer","minimum":8,"maximum":128},"rows":{"type":"integer","minimum":3,"maximum":32},"gravity_m_s2":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"translation_mm":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"initial_velocity_m_s":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"ground":{"type":"boolean"},"ground_z_mm":{"type":"number"},"friction":{"type":"number","minimum":0,"maximum":2},"damping_per_s":{"type":"number","minimum":0,"maximum":100},"fixed_nodes":{"type":"array","items":{"type":"integer","minimum":0}},"pulleys":{"type":"array","maxItems":8,"items":{"type":"object"}}},"additionalProperties":false})");}
V inspectRubber(AgentSession& s,const V& args){
    keys(args,{"expected_revision","rubber"});const auto& a=field(args,"rubber");
    if(word(a,"action","run")=="capabilities"){keys(a,{"action"});return rubberCapabilities();}
    revision(s,args);need(word(a,"action","run")=="run","Unknown study action");keys(a,{"action","id","backend","duration_s","step_s","iterations","output_fps","around","rows","gravity_m_s2","translation_mm","initial_velocity_m_s","ground","ground_z_mm","friction","damping_per_s","fixed_nodes","pulleys"});
    const double idNumber=number(a,"id",0);need(idNumber>0&&idNumber<=9007199254740991.&&std::floor(idNumber)==idNumber,"Study needs exact feature id");const u64 id=(u64)idNumber;auto* f=s.document.find(id);
    need(f&&f->type==FeatureType::NativeSolid&&word(f->nativeSpec,"op")=="rubber_body","Select an authored rubber_body definition; transformed/modified bodies need a new explicit analysis recipe");
    need(!f->nativeSpec.find("relief"),"Exact relief changes the mass distribution; this surface-network study currently supports smooth authored rubber bodies only");
    const auto kind=word(f->nativeSpec,"kind");const Material* material=nullptr;for(const auto& m:s.document.materials)if(m.name==f->material)material=&m;need(material&&material->densityGcm3>0,"Assign a declared material density");
    const double young=number(material->physical,"young_modulus_Pa",0);need(young>0&&young<=1e11,"Declare positive material young_modulus_Pa (<=1e11) for the reduced network");
    V q=json(R"({"queries":[{"action":"properties"}]})");q.set("id",V::makeNum((double)id));auto queries=*q.find("queries");queries.arr[0].set("density_g_cm3",V::makeNum(material->densityGcm3));q.set("queries",queries);const auto response=s.call("geometry_query",q);const auto& properties=field(accepted(response),"results").arr.at(0);need(field(properties,"ok").b,"Exact native mass query failed");const double mass=number(properties,"mass_g",0)*.001;need(mass>0,"Native mass must be positive");const double volume=mass/(material->densityGcm3*1000);
    const auto env=s.document.buildEnv();auto dim=[&](const char* key){const double n=f->num(env,key);need(std::isfinite(n),std::string("Invalid source dimension ")+key);return n;};
    const unsigned around=integer(a,"around",32,8,128),rows=integer(a,"rows",8,3,32);std::vector<Vec3> points;std::vector<unsigned> triangles;
    V recipe=V::makeObj(),surface=V::makeObj();bool closedRows=kind=="tire";
    if(kind=="belt"||kind=="track"){surface.set("kind",V::makeStr("racetrack"));for(auto k:{"radius_mm","center_distance_mm","width_mm"})surface.set(k,V::makeNum(dim(k)));}
    else if(kind=="tire"){surface.set("kind",V::makeStr("torus"));for(auto k:{"major_radius_mm","radial_radius_mm","axial_radius_mm"})surface.set(k,V::makeNum(dim(k)));}
    else{need(kind=="ball","Unsupported rubber kind");surface.set("kind",V::makeStr("sphere"));surface.set("radius_mm",V::makeNum(dim("radius_mm")));}
    recipe.set("surface",surface);recipe.set("distribution",V::makeStr("grid"));recipe.set("around",V::makeNum(around));recipe.set("rows",V::makeNum(rows));RubberPatternPlan plan(recipe);
    const Vec3 translation=a.find("translation_mm")?vector(*a.find("translation_mm")):Vec3{};need(translation.len()<1e7,"Study translation exceeds bounds");for(unsigned j=0;j<rows;++j)for(unsigned i=0;i<around;++i)points.push_back((plan.frame(j*around+i).position_mm+translation)*.001);
    auto triangle=[&](unsigned x,unsigned y,unsigned z){triangles.insert(triangles.end(),{x,y,z});};
    for(unsigned j=0;j<(closedRows?rows:rows-1);++j)for(unsigned i=0;i<around;++i){const unsigned ni=(i+1)%around,nj=(j+1)%rows;triangle(j*around+i,j*around+ni,nj*around+ni);triangle(j*around+i,nj*around+ni,nj*around+i);}
    if(kind=="ball"){const unsigned north=(unsigned)points.size(),south=north+1;const double radius=dim("radius_mm");points.push_back((translation+Vec3{0,0,radius})*.001);points.push_back((translation-Vec3{0,0,radius})*.001);for(unsigned i=0;i<around;++i){triangle(north,(i+1)%around,i);triangle(south,(rows-1)*around+i,(rows-1)*around+(i+1)%around);}}
    need(points.size()<=4096,"Study exceeds 4096 nodes; reduce around/rows");
    std::vector<double> weights(points.size(),0);std::set<std::pair<unsigned,unsigned>> edges;
    auto edge=[&](unsigned x,unsigned y){if(x!=y)edges.insert(std::minmax(x,y));};
    for(size_t t=0;t<triangles.size();t+=3){unsigned x=triangles[t],y=triangles[t+1],z=triangles[t+2];double area=(points[y]-points[x]).cross(points[z]-points[x]).len()*.5;need(area>1e-18,"Degenerate analysis triangle");for(auto n:{x,y,z})weights[n]+=area/3;edge(x,y);edge(y,z);edge(z,x);}
    // Opposite diagonals prevent directional bias; two-hop springs resist folds.
    for(unsigned j=0;j<rows;++j)for(unsigned i=0;i<around;++i){edge(j*around+i,j*around+(i+2)%around);if(closedRows||j+1<rows)edge(j*around+(i+1)%around,((j+1)%rows)*around+i);if(closedRows||j+2<rows)edge(j*around+i,((j+2)%rows)*around+i);}
    double area=0,totalLength=0;for(auto w:weights)area+=w;for(auto e:edges)totalLength+=(points[e.second]-points[e.first]).len();need(area>0&&totalLength>0,"Empty analysis network");
    const double section=volume/totalLength;std::vector<RubberNode> nodes;const Vec3 velocity=a.find("initial_velocity_m_s")?vector(*a.find("initial_velocity_m_s")):Vec3{};need(velocity.len()<=100,"Initial speed exceeds 100 m/s");
    for(size_t i=0;i<points.size();++i){RubberNode node;node.position=points[i];node.velocity=velocity;node.mass=mass*weights[i]/area;nodes.push_back(node);}
    if(const auto* fixed=a.find("fixed_nodes")){need(fixed->t==V::Arr,"fixed_nodes must be an array");std::set<unsigned> seen;for(const auto& n:fixed->arr){need(n.t==V::Num&&n.num>=0&&n.num<nodes.size()&&n.num==std::floor(n.num),"Invalid fixed node index");need(seen.insert((unsigned)n.num).second,"Duplicate fixed node");nodes[(unsigned)n.num].fixed=true;}}
    std::vector<RubberLink> links;for(auto e:edges){const double length=(points[e.second]-points[e.first]).len();links.push_back({e.first,e.second,length,length/(young*section)});}
    RubberDynamicsOptions options;options.backend=word(a,"backend","gpu");options.duration=number(a,"duration_s",1);options.step=number(a,"step_s",1./240);options.iterations=integer(a,"iterations",8,1,32);options.outputFps=integer(a,"output_fps",30,1,60);need(options.duration>0&&options.duration<=10,"Duration must be 0..10 seconds");need((std::ceil(options.duration*options.outputFps)+1)*points.size()<=262144,"Result exceeds 262144 node frames; reduce duration, fps or resolution");options.gravity=a.find("gravity_m_s2")?vector(*a.find("gravity_m_s2")):Vec3{0,0,-9.80665};options.ground=boolean(a,"ground",true);options.groundZ=number(a,"ground_z_mm",0)*.001;options.friction=number(a,"friction",.7);options.damping=number(a,"damping_per_s",2);
    if(kind=="belt"||kind=="track")options.contactRadius=dim("thickness_mm")*.0005;
    if(const auto* pulleys=a.find("pulleys")){need(pulleys->t==V::Arr&&pulleys->arr.size()<=8,"Use at most 8 pulleys");for(const auto& p:pulleys->arr){keys(p,{"center_mm","axis","radius_mm","angular_velocity_rad_s"});RubberPulley pulley;pulley.center=vector(field(p,"center_mm"))*.001;pulley.axis=vector(field(p,"axis"));pulley.radius=number(p,"radius_mm",0)*.001;pulley.angularVelocity=number(p,"angular_velocity_rad_s",0);options.pulleys.push_back(pulley);}}
    const auto simulation=simulateRubber(nodes,links,options);V out=V::makeObj();out.set("revision",V::makeNum((double)s.revision));out.set("source_sha256",V::makeStr(iterationSha256(documentToJson(s.document,false,false))));out.set("id",V::makeNum((double)id));out.set("nodes_count",V::makeNum((double)nodes.size()));out.set("links_count",V::makeNum((double)links.size()));out.set("mass_kg",V::makeNum(mass));out.set("mass_authority",V::makeStr("exact_native_volume_with_declared_density; area-weighted surface lumping"));out.set("stiffness_authority",V::makeStr("reduced XPBD spring network; A_eff=CAD_volume/sum_link_lengths; compliance=L/(E*A_eff); not a calibrated continuum constitutive law"));out.set("diagnostics",simulation.diagnostics);out.set("limits",V::makeStr("Single local rubber definition. Surface network with distance/shear/fold links, discrete ground/pulley contacts and friction. No self-contact, pressure, volume conservation, tire handling law, thermal effects, fatigue or FEA. Large strains and discrete tunnelling require validation; no manufacturing qualification. Playback never changes CAD."));
    V tris=V::makeArr(),frames=V::makeArr();for(auto t:triangles)tris.arr.push_back(V::makeNum(t));out.set("triangles",tris);for(size_t i=0;i<simulation.frames.size();++i){V frame=V::makeObj(),positions=V::makeArr();frame.set("time_s",V::makeNum(simulation.times.at(i)));for(auto p:simulation.frames[i])positions.arr.push_back(vector(p*1000));frame.set("positions_mm",positions);frames.arr.push_back(frame);}out.set("frames",frames);return out;
}
}
