#include "ark/agent.hpp"
#include "ark/sketch.hpp"
#include <cctype>
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
V json(const char* s){V v;check(arkjson::parse(s,v),"test JSON parses");return v;}
// Guide text writes objects relaxed: {key:value} with bare words and 'quoted' strings.
std::string relaxedJson(const std::string& text){std::string out;for(size_t i=0;i<text.size();){const char c=text[i];
    if(c=='\''){const size_t end=text.find('\'',i+1);check(end!=std::string::npos,"Guide text closes its quote");out+='"'+text.substr(i+1,end-i-1)+'"';i=end+1;}
    else if(std::isalpha((unsigned char)c)||c=='_'){size_t end=i;while(end<text.size()&&(std::isalnum((unsigned char)text[end])||text[end]=='_'))++end;out+='"'+text.substr(i,end-i)+'"';i=end;}
    else{out+=c;++i;}}return out;}
V call(AgentSession& s,const char* name,const char* args){auto v=s.call(name,json(args));if(v.find("isError")->b)throw std::runtime_error(v.dump());return *v.find("structuredContent");}
void rejected(AgentSession& s,const char* args){auto before=documentToJson(s.document);auto rev=s.revision;check(s.call("doc_apply",json(args)).find("isError")->b,"invalid operation rejected");check(s.revision==rev&&documentToJson(s.document)==before,"failed operation rolls back all state");}
double distance(Vec2 p,const std::vector<Vec2>& poly){double d=1e100;for(size_t i=1;i<poly.size();++i){auto ab=poly[i]-poly[i-1];auto t=clamp((p-poly[i-1]).dot(ab)/ab.lensq(),0.,1.);d=std::min(d,(p-poly[i-1]-ab*t).len());}return d;}
// Independent Barry-Goldman interpolation, not the production Bezier conversion.
Vec2 referenceSpline(Vec2 p0,Vec2 p1,Vec2 p2,Vec2 p3,double u){
    double t0=0,t1=std::sqrt((p1-p0).len()),t2=t1+std::sqrt((p2-p1).len()),t3=t2+std::sqrt((p3-p2).len()),t=t1+u*(t2-t1);
    auto blend=[](Vec2 a,Vec2 b,double ta,double tb,double x){return a*((tb-x)/(tb-ta))+b*((x-ta)/(tb-ta));};
    auto a1=blend(p0,p1,t0,t1,t),a2=blend(p1,p2,t1,t2,t),a3=blend(p2,p3,t2,t3,t);
    return blend(blend(a1,a2,t0,t2,t),blend(a2,a3,t1,t3,t),t1,t2,t);
}
}
int main(){try{
    AgentSession s;
    auto setup=R"({"commands":[
      {"op":"param.set","name":"width","expr":"100"},{"op":"param.set","name":"depth","expr":"50"},{"op":"param.set","name":"thickness","expr":"8"},{"op":"param.set","name":"margin","expr":"15"},
      {"op":"plane.set","name":"fixture","p":{"ox":"13","oy":"-7","oz":"20","rx":"23","rz":"31"}},
      {"op":"plane.set","name":"stock","parent":"fixture","p":{"oz":"2"}},
      {"op":"sketch.set","name":"base","sketch":{"plane":"stock","entities":[
        {"name":"bottom","kind":"line","dof":["0","0","width","0"]},{"name":"right","kind":"line","dof":["width","0","width","depth"]},
        {"name":"top","kind":"line","dof":["width","depth","0","depth"]},{"name":"left","kind":"line","dof":["0","depth","0","0"]}]}},
      {"op":"feature.add","type":"extrude","p":{"sketch":"base","height":"thickness"}},
      {"op":"sketch.set","name":"holes","sketch":{"plane":"stock","tolerance":0.01,"entities":[
        {"name":"left_hole","kind":"circle","dof":["margin","depth/2","4"]},
        {"name":"right_hole","kind":"circle","dof":["width-margin","depth/2","4"]}]}},
      {"op":"feature.add","type":"sketch_cut","inputs":[1],"p":{"sketch":"holes","depth":"thickness+2","offset":"-1"}}
    ]})";
    call(s,"doc_apply",setup);
    auto verify=[&](double width,double depth){
        auto frame=resolvePlane(s.document,"stock");auto mesh=s.document.evalBodies.back().tessellate(s.document.tessOpt);MeshBVH bvh(mesh);
        check(analyzeMesh(mesh).closedOriented(),"Parametric through-cut output has valid closed indexed topology");
        auto hit=[&](double x,double y){return bvh.raycast(mesh,{frame.transformPoint({x,y,30}),frame.transformDir({0,0,-1})}).hit;};
        check(!hit(15,depth/2)&&!hit(width-15,depth/2),"Both actual holes remain through the stock");
        check(hit(width/2,depth/2)&&hit(2,2),"Material retained between and outside holes");
        auto hole=s.document.findSketch("holes");check(std::abs(hole->sk.ents[1].dof[0]-(width-15))<1e-12,"Right hole keeps edge margin");
        check(hole->freeDofs==0,"Expression-driven hole is fully constrained");
        // Integral of the produced mesh; mesh CSG topology is independently reported, not certified.
        double volume=0;for(size_t i=0;i<mesh.idx.size();i+=3){auto point=[&](u32 k){return Vec3{mesh.pos[3*k],mesh.pos[3*k+1],mesh.pos[3*k+2]};};auto a=point(mesh.idx[i]),b=point(mesh.idx[i+1]),c=point(mesh.idx[i+2]);volume+=a.dot(b.cross(c))/6;}
        double expected=(width*depth-2*K_PI*16)*8;check(std::abs(volume-expected)<expected*.001,"Cut volume agrees with analytic holed stock within circle approximation");
    };
    verify(100,50);
    call(s,"doc_apply",R"({"commands":[{"op":"param.set","name":"width","expr":"140"},{"op":"param.set","name":"depth","expr":"60"}]})");verify(140,60);
    call(s,"doc_undo","{}");verify(100,50);call(s,"doc_redo","{}");verify(140,60);
    std::string saved=documentToJson(s.document),error;Document loaded;check(documentFromJson(saved,loaded,&error),"v5 reads");check(saved==documentToJson(loaded),"v5 expressions and planes roundtrip canonically");loaded.recompute();check(loaded.lastResults.back().ok,"Loaded document recomputes");
    rejected(s,R"({"commands":[{"op":"plane.set","name":"fixture","parent":"stock"}]})");
    rejected(s,R"({"commands":[{"op":"plane.remove","name":"stock"}]})");
    rejected(s,R"({"commands":[{"op":"sketch.remove","name":"holes"}]})");
    rejected(s,R"({"commands":[{"op":"param.set","name":"width","expr":"1/0"}]})");
    rejected(s,R"({"commands":[{"op":"sketch.entity.set","name":"base","entity":{"name":"right","kind":"line","dof":["width","0","width","depth+1"]}}]})");
    rejected(s,R"({"commands":[{"op":"plane.set","name":"XY","p":{"oz":"2"}}]})");
    rejected(s,R"({"commands":[{"op":"plane.set","name":"far","p":{"ox":"10000000"}},{"op":"feature.add","type":"extrude","p":{"plane":"far","shape":"rect","w":"0.01","d":"0.01","height":"1"}}]})");
    rejected(s,R"({"commands":[{"op":"sketch.set","name":"bad","sketch":{"entities":[{"kind":"point","dof":["3","0"]}],"constraints":[{"type":"fixedX","a":0,"val":"4"}]}}]})");
    call(s,"doc_apply",R"({"commands":[{"op":"sketch.set","name":"dimensioned","sketch":{"entities":[{"kind":"circle","dof":[0,0,2]}],"constraints":[{"type":"fixed","a":0,"val":"width/2","val2":"depth/2"},{"type":"radius","a":0,"val":"margin/3"}]}}]})");
    auto dimensioned=s.document.findSketch("dimensioned");check(dimensioned->freeDofs==0&&dimensioned->rank==3,"Local constraint rank");check(std::abs(dimensioned->sk.ents[0].dof[0]-70)<1e-6,"Dimension expressions solve circle position");
    call(s,"doc_apply",R"({"commands":[{"op":"sketch.set","name":"loose","sketch":{"entities":[{"kind":"circle","dof":[0,0,3]}],"constraints":[{"type":"radius","a":0,"val":3}]}}]})");
    check(s.document.findSketch("loose")->freeDofs==2,"Underconstrained status counts two center coordinates");
    call(s,"doc_apply",R"({"commands":[{"op":"sketch.set","name":"angle","sketch":{"entities":[{"kind":"line","dof":["0","0","1","0"]},{"kind":"line","dof":["0","0","0","1"]}],"constraints":[{"type":"angle","a":0,"b":1,"val":"3*pi/2"}]}}]})");
    check(s.document.findSketch("angle")->residual<1e-12,"Angle target is periodic modulo 2pi");

    // A concave polygon whose vertex lies exactly on a candidate ear diagonal.
    // Previously yielded missing/degenerate cap triangles despite positive area.
    for(bool reverse:{false,true})for(double scale:{.1,1.,25.}){
        std::vector<Vec2> polygon{{0,0},{10,0},{10,10},{5,5},{0,10}};
        if(reverse)std::reverse(polygon.begin(),polygon.end());SketchProfile p;p.frame=Mat4::translation({12,-9,7})*Mat4::rotX(.3);
        for(auto& point:polygon)point*=scale;
        Sketch outline;for(size_t i=0;i<polygon.size();++i){auto a=polygon[i],b=polygon[(i+1)%polygon.size()];outline.addLine(a.x,a.y,b.x,b.y);}
        p=sketchProfiles(outline,p.frame).front();
        auto audit=analyzeMesh(extrudeProfile(p,3).tessellate({}));
        check(audit.closedOriented()&&audit.degenerateTriangles==0,"Concave cap has no missing/degenerate triangles");
        check(std::abs(audit.signedVolume-225*scale*scale)<std::max(1e-5,225*scale*scale*1e-5),"Concave prism volume matches independent polygon area");
    }

    SketchEnt spline;spline.kind=SketchEnt::Spline;spline.dof={0,0,8,15,27,-6,40,4};auto poly=sampleSketchEntity(spline,.015);
    check(poly.front().approx({0,0},1e-12)&&poly.back().approx({40,4},1e-12),"Spline interpolates endpoints");
    std::vector<Vec2> anchors{{0,0},{8,15},{27,-6},{40,4}};
    for(int k=0;k<3;++k)for(int i=0;i<=200;++i){auto p0=k?anchors[k-1]:anchors[0]*2-anchors[1],p3=k<2?anchors[k+2]:anchors[3]*2-anchors[2];auto p=referenceSpline(p0,anchors[k],anchors[k+1],p3,i/200.);check(distance(p,poly)<=.01500001,"Adaptive curve samples meet requested chord tolerance against independent evaluator");}
    auto fine=sampleSketchEntity(spline,.003);check(fine.size()>poly.size(),"Tighter tolerance refines curve");
    auto shifted=spline;for(size_t i=0;i<shifted.dof.size();i+=2){shifted.dof[i]+=123;shifted.dof[i+1]-=87;}auto translated=sampleSketchEntity(shifted,.015);check(poly.size()==translated.size(),"Translation does not change subdivision");for(size_t i=0;i<poly.size();++i)check((poly[i]+Vec2{123,-87}).approx(translated[i],1e-10),"Spline translation equivariance");

    AgentSession freeform;
    auto onboarding=call(freeform,"agent_onboard",R"({"topic":"sketch"})");auto example=*onboarding.find("example_doc_apply");
    // The guide's freeform example builds an exact native extrusion and names
    // what the sampled legacy types build. Portable builds without the native
    // kernel exercise the same sketch through the legacy extrude instead.
    {const auto& last=example.find("commands")->arr.back();check(last.find("type")->str=="native_solid"&&last.find("native")->find("op")->str=="extrude"&&example.dump().find("\"type\":\"extrude\"")==std::string::npos,"Sketch guide example authors an exact native extrusion");
        const auto rules=onboarding.find("rules")->dump();check(rules.find("Legacy types extrude, revolve and sketch_cut build sampled mesh solids")!=std::string::npos&&rules.find("revolve_surface p:{sketch,angle:360,solid:1}")!=std::string::npos&&rules.find("Recipe equation2d is an authored adaptive polyline")!=std::string::npos,"Sketch guide names sampled legacy solids and exact curved alternatives");
        const auto solids=call(freeform,"agent_onboard",R"({"topic":"solids"})").find("rules")->dump();check(solids.find("Exact curved forms, when the design calls for them: fillet/chamfer above; feature.add types revolve_surface p:{sketch,angle,solid:1} with native.axis X|Y|Z, loft native:{sections:[sketch names]} p:{solid:1} and sweep native:{section,path} p:{solid:1}.")!=std::string::npos,"Solids guide offers exact curved operations without requiring them");}
    // The executed follow-up hole is parsed from the guide's own next_step text.
    V holeNative;{const auto next=onboarding.find("next_step")->str;const size_t at=next.find("native:{op:hole,cutter:{");check(next.find("add native_solid inputs:[returned extrusion ID], native:{")!=std::string::npos&&at!=std::string::npos,"Sketch guide next step names a native hole");
        size_t end=at+7;for(int depth=0;end<next.size();++end){depth+=next[end]=='{';depth-=next[end]=='}';if(!depth)break;}check(end<next.size(),"Guide hole object is closed");
        holeNative=json(relaxedJson(next.substr(at+7,end-at-6)).c_str());const auto* cutter=holeNative.find("cutter");check(cutter&&cutter->t==V::Obj,"Guide hole names its cutter");
        auto field=[&](const char* key){const V* value=cutter->find(key);check(value!=nullptr,"Guide hole names every cutter field it needs");return *value;};
        const double plate=std::stod(example.find("commands")->arr.back().find("p")->find("depth")->str);
        check(field("kind").str=="circle"&&field("plane").str=="design"&&field("cx").num==0&&field("cy").num==0&&field("radius").num>0&&field("offset").num<0&&field("offset").num+field("depth").num>plate,"Guide hole states a circle cutter on the example plane that spans the plate");}
#ifndef ARK_SKETCH_NATIVE_TESTS
    {V commands=*example.find("commands");commands.arr.back()=json(R"({"op":"feature.add","type":"extrude","name":"Freeform plate","p":{"sketch":"outline","height":"6"}})");example.set("commands",commands);}
#endif
    check(!freeform.call("doc_apply",example).find("isError")->b,"Onboarding example executes in a fresh document");
#ifdef ARK_SKETCH_NATIVE_TESTS
    {// Six closed-spline spans stay six exact side faces; the guide's follow-up hole stays exact too.
        AgentSession exact;check(!exact.call("doc_apply",example).find("isError")->b,"Exact onboarding example executes");const auto id=std::to_string(exact.document.features.back().id);
        auto plate=*call(exact,"native_inspect",("{\"id\":"+id+"}").c_str()).find("report");check(plate.find("brep_valid")->b&&plate.find("faces")->num==8,"Freeform plate keeps exact spline side faces");
        V add=json(R"({"op":"feature.add","type":"native_solid","name":"Hole"})"),inputs=V::makeArr(),commands=V::makeArr(),hole=V::makeObj();inputs.arr.push_back(V::makeNum((double)exact.document.features.back().id));add.set("inputs",inputs);add.set("native",holeNative);commands.arr.push_back(add);hole.set("expected_revision",V::makeNum((double)exact.revision));hole.set("commands",commands);
        check(!exact.call("doc_apply",hole).find("isError")->b,"Guide follow-up hole executes on the exact plate");
        auto bored=*call(exact,"native_inspect",("{\"id\":"+std::to_string(exact.document.features.back().id)+"}").c_str()).find("report");const double radius=holeNative.find("cutter")->find("radius")->num,thickness=std::stod(example.find("commands")->arr.back().find("p")->find("depth")->str);
        check(bored.find("brep_valid")->b&&bored.find("faces")->num==9&&std::abs(plate.find("volume_mm3")->num-bored.find("volume_mm3")->num-K_PI*radius*radius*thickness)<1e-3,"Guide hole is an exact through-cut");}
#endif
    auto report=call(freeform,"sketch_inspect",R"({"name":"outline","samples":true})");check(report.find("profile_ready")->b&&report.find("remaining_dofs")->num==0,"Closed spline profile ready and driven");
    auto before=freeform.document.evalBodies[0].tessellate({}).computeBBox();
    call(freeform,"doc_apply",R"({"commands":[{"op":"param.set","name":"width","expr":"160"}]})");auto after=freeform.document.evalBodies[0].tessellate({}).computeBBox();check(after.extent().x>before.extent().x*1.5,"Freeform shape changes with width parameter");
    call(freeform,"doc_apply",R"({"commands":[{"op":"sketch.entity.set","name":"outline","entity":{"name":"skin","kind":"spline","closed":true,"dof":["-width/2","0","0","-height/2","width/2","0","0","height/2"]}}]})");check(freeform.document.findSketch("outline")->sk.ents.size()==1,"Named spline edit replaces rather than duplicates");
    rejected(freeform,R"({"commands":[{"op":"sketch.entity.set","name":"outline","entity":{"name":"skin","kind":"spline","closed":true,"dof":[0,0,0,0,10,10]}}]})");
    rejected(freeform,R"({"commands":[{"op":"sketch.set","name":"outline","sketch":{"entities":[{"kind":"line","dof":[0,0,10,10]},{"kind":"line","dof":[10,10,0,10]},{"kind":"line","dof":[0,10,10,0]},{"kind":"line","dof":[10,0,0,0]}]}}]})");
    // Symmetric extrusion must offset once in LOCAL plane coordinates.
    SketchProfile profile;profile.frame=Mat4::rotX(.7)*Mat4::rotY(.4);profile.segs={{ProfileSeg::Line,{0,0},{10,0}},{ProfileSeg::Line,{10,0},{10,10}},{ProfileSeg::Line,{10,10},{0,10}},{ProfileSeg::Line,{0,10},{0,0}}};
    auto symmetric=extrudeProfile(profile,8,true).tessellate({});auto inverse=profile.frame.inverted();double zmin=1e100,zmax=-1e100;
    for(size_t i=0;i<symmetric.pos.size();i+=3){auto p=inverse.transformPoint({symmetric.pos[i],symmetric.pos[i+1],symmetric.pos[i+2]});zmin=std::min(zmin,p.z);zmax=std::max(zmax,p.z);}check(std::abs(zmin+4)<2e-6&&std::abs(zmax-4)<2e-6,"Symmetric extrusion respects rotated plane");
    auto previous=documentToJson(freeform.document,true,false);auto revision=freeform.revision;
    V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)revision));
    check(!freeform.call("doc_new",fresh).find("isError")->b&&freeform.document.features.empty(),"New document clears the active model");
    check(freeform.revision==revision+1,"New document never resets revision");
    check(freeform.call("doc_new",fresh).find("isError")->b,"Stale new-document request rejected");
    call(freeform,"doc_undo","{}");check(documentToJson(freeform.document,true,false)==previous,"New document can be undone without overwriting files");
    auto revisionBeforeOnboard=freeform.revision;call(freeform,"agent_onboard","{}");check(freeform.revision==revisionBeforeOnboard,"Onboarding is read-only");
    std::cout<<checks<<" parametric sketch checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"After "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
