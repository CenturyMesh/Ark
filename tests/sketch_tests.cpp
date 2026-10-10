#include "ark/agent.hpp"
#include "ark/sketch.hpp"
#include <iostream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
V json(const char* s){V v;check(arkjson::parse(s,v),"test JSON parses");return v;}
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
    auto onboarding=call(freeform,"agent_onboard",R"({"topic":"sketch"})");auto example=*onboarding.find("example_doc_apply");check(!freeform.call("doc_apply",example).find("isError")->b,"Onboarding example executes in a fresh document");
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
