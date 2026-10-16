#include "ark/sketch.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const V& field(const V& value,const char* key){auto* found=value.find(key);if(!found)throw std::runtime_error(std::string("Missing diagnosis field: ")+key);return *found;}
const V& constraint(const V& report,size_t index){return field(report,"constraints").arr.at(index);}
}

int main(){try{
    {
        Sketch loose;int line=loose.addLine(0,0,12,1);loose.cons.push_back({SketchCon::Horizontal,line});
        check(solveSketch(loose)<1e-6,"Horizontal line converges");
        auto d=diagnoseSketch(loose);
        check(field(d,"variables").num==4&&field(d,"jacobian_rank").num==1&&field(d,"remaining_dofs").num==3,"Local rank identifies three free coordinates");
        check(field(d,"constraint_status").str=="underconstrained"&&!field(constraint(d,0),"violated").b,"Underconstrained constraint is satisfied");
    }
    {
        Sketch full;int point=full.addPoint(2,3);SketchCon fixed;fixed.type=SketchCon::Fixed;fixed.a=point;fixed.val=8;fixed.val2=-4;fixed.id="origin";full.cons.push_back(fixed);
        check(solveSketch(full)<1e-6,"Fixed point converges");auto d=diagnoseSketch(full);
        check(field(d,"remaining_dofs").num==0&&field(d,"jacobian_rank").num==2,"Fixed point has full local rank");
        check(field(constraint(d,0),"rank_gain").num==2&&field(constraint(d,0),"id").str=="origin","Two-row constraint and stable ID reported");
    }
    {
        Sketch redundant;int point=redundant.addPoint(1,2);SketchCon first;first.type=SketchCon::FixedX;first.a=point;first.val=4;first.id="first";redundant.cons.push_back(first);first.id="repeat";redundant.cons.push_back(first);
        check(solveSketch(redundant)<1e-6,"Duplicate target converges");auto d=diagnoseSketch(redundant);
        check(field(d,"jacobian_rank").num==1&&field(d,"remaining_dofs").num==1,"Duplicate does not add rank");
        check(!field(constraint(d,0),"locally_redundant").b&&field(constraint(d,1),"locally_redundant").b,"Second duplicate is locally redundant");
        check(field(d,"locally_redundant_constraints").num==1&&field(d,"violated_constraints").num==0,"Redundancy is separate from conflict");
    }
    {
        Sketch conflicting;int point=conflicting.addPoint(5,0);SketchCon first;first.type=SketchCon::FixedX;first.a=point;first.val=0;conflicting.cons.push_back(first);first.val=10;conflicting.cons.push_back(first);
        check(solveSketch(conflicting)>1,"Incompatible fixed X targets do not converge");auto d=diagnoseSketch(conflicting);
        check(field(d,"constraint_status").str=="conflicting"&&field(d,"violated_constraints").num==2,"Both incompatible targets are conflict candidates");
        check(field(constraint(d,0),"max_residual").num>1&&field(constraint(d,1),"max_residual").num>1,"Per-constraint residuals expose conflict");
        check(field(constraint(d,1),"locally_redundant").b,"Rank-dependence remains explicit on contradictory constraint");
        check(!field(constraint(d,0),"suggestion").str.empty(),"Conflict candidates carry an explicit review action");
        check(field(constraint(d,0),"entities").arr.size()==1,"Unary constraints expose one unique affected entity");
        NamedSketch named;named.name="Conflict";named.sk=conflicting;named.sk.ents[0].name="mount";named.sk.cons[0].id="left";named.sk.cons[1].id="right";
        const auto original=named.sk.cons;evaluateSketch(named,ExprEnv{});
        check(named.error.find("left")!=std::string::npos&&named.error.find("right")!=std::string::npos&&named.error.find("mount")!=std::string::npos,"Failed solve identifies authored constraint IDs and named geometry");
        check(named.sk.cons.size()==original.size()&&named.sk.cons[0].val==original[0].val&&named.sk.cons[1].val==original[1].val,"Diagnosis does not remove relations or change targets");
    }
    {
        NamedSketch named;named.name="dimensioned";named.sk.addCircle(0,0,2);SketchCon center;center.type=SketchCon::Fixed;center.a=0;center.expression="width/2";center.expression2="height/2";center.id="center";named.sk.cons.push_back(center);
        SketchCon radius;radius.type=SketchCon::Radius;radius.a=0;radius.expression="hole_r";radius.id="radius";named.sk.cons.push_back(radius);
        ExprEnv env;env.vars={{"width",80},{"height",40},{"hole_r",5}};evaluateSketch(named,env);
        check(named.error.empty()&&named.freeDofs==0&&std::abs(named.sk.ents[0].dof[0]-40)<1e-8&&std::abs(named.sk.ents[0].dof[2]-5)<1e-8,"Dimension expressions resolve and constrain circle");
        env.vars["width"]=120;env.vars["hole_r"]=7;evaluateSketch(named,env);auto d=diagnoseSketch(named.sk);
        check(named.error.empty()&&std::abs(named.sk.ents[0].dof[0]-60)<1e-8&&std::abs(named.sk.ents[0].dof[2]-7)<1e-8,"Parameter edit updates saved dimensions");
        check(named.sk.cons[0].expression=="width/2"&&named.sk.cons[1].expression=="hole_r"&&field(d,"remaining_dofs").num==0,"Expressions remain attached after evaluation");
    }
    {
        NamedSketch tangent;tangent.name="Profile";std::string error;
        check(sketchFromJson(R"({"entities":[{"name":"Base","kind":"line","dof":[0,0,20,0]},{"name":"Rim","kind":"arc","dof":[0,8,"5","-90","90"]}],"constraints":[{"type":"fixed","a":0,"pa":0,"val":0,"val2":0},{"type":"fixed","a":0,"pa":1,"val":20,"val2":0},{"type":"tangent","a":0,"b":1}]})",tangent.sk,&error),"Tangent fixture parses");
        evaluateSketch(tangent,ExprEnv{});
        if(!tangent.error.empty()){std::cerr<<"Tangent null direction: "<<tangent.error<<" dofs=";for(const auto& entity:tangent.sk.ents)for(double value:entity.dof)std::cerr<<value<<',';std::cerr<<'\n';}
        check(tangent.error.empty()&&tangent.residual<1e-6,"Fixed-line tangency must converge without magnifying its free translation");
        check(std::abs(tangent.sk.ents[1].dof[0])<1e-3&&std::abs(tangent.sk.ents[1].dof[1]-5)<1e-6,"An almost-null tangent direction must not move the arc center arbitrarily far");
    }
    for(double rotation:{.3,.9,1.5}){
        const double cx=std::cos(rotation),cy=std::sin(rotation);Sketch sketch;
        sketch.addLine(30,-40,30+20*cx,-40+20*cy);
        SketchEnt arc;arc.kind=SketchEnt::Arc;arc.dof={30-8*cy,-40+8*cx,5,-90,90};arc.expressions={"","","5","-90","90"};sketch.ents.push_back(arc);
        SketchCon fixed;fixed.type=SketchCon::Fixed;fixed.a=0;fixed.val=30;fixed.val2=-40;sketch.cons.push_back(fixed);fixed.pa=1;fixed.val=30+20*cx;fixed.val2=-40+20*cy;sketch.cons.push_back(fixed);
        SketchCon tangent;tangent.type=SketchCon::Tangent;tangent.a=0;tangent.b=1;sketch.cons.push_back(tangent);
        for(int repeat=0;repeat<3;++repeat)check(solveSketch(sketch)<1e-6,"Rotated/translated tangent system retains physical residual acceptance on repeated solves");
        const Vec2 delta{sketch.ents[1].dof[0]-30,sketch.ents[1].dof[1]+40};
        check(std::abs(delta.x*cx+delta.y*cy)<1e-3&&std::abs(-delta.x*cy+delta.y*cx-5)<1e-6,"Null-direction regularization is not dependent on global axis alignment");
    }
    for(double length:{1e-4,1.,1e6}){
        Sketch scaled;scaled.addLine(0,0,1,0);scaled.ents[0].expressions={"0","0","1","0"};
        scaled.addLine(0,0,length*.8,length*.2);scaled.ents[1].expressions={"0","0","",""};
        SketchCon distance;distance.type=SketchCon::DistPP;distance.a=distance.b=1;distance.pb=1;distance.val=length;scaled.cons.push_back(distance);
        SketchCon direction;direction.type=SketchCon::AngleLL;direction.a=0;direction.b=1;direction.val=-.7;scaled.cons.push_back(direction);
        const double residual=solveSketch(scaled);
        if(!(residual<1e-6))std::cerr<<"Scaled sketch length="<<length<<" residual="<<residual<<'\n';
        check(residual<1e-6,"Equilibrated QR resolves mixed length/angle constraints at small and large scales");
        check(std::abs(std::atan2(scaled.ents[1].dof[3],scaled.ents[1].dof[2])-.7)<1e-6,"Angular acceptance is unchanged at every length scale");
        check(scaled.ents[0].dof[2]==1&&scaled.ents[1].expressions[0]=="0","Driven geometry and authored expressions survive scaled solves");
    }
    for(auto type:{SketchCon::Parallel,SketchCon::Perpendicular,SketchCon::AngleLL,SketchCon::Tangent}){
        Sketch degenerate;degenerate.addLine(0,0,0,0);if(type==SketchCon::Tangent)degenerate.addCircle(0,0,1);else degenerate.addLine(0,0,1,0);
        SketchCon con;con.type=type;con.a=0;con.b=1;degenerate.cons.push_back(con);
        check(!std::isfinite(solveSketch(degenerate)),"Zero-length angular/tangent geometry must reject instead of reporting satisfaction");
    }
    {
        Sketch conflict;conflict.addPoint(4,3);conflict.ents[0].expressions={"","3"};
        SketchCon con;con.type=SketchCon::FixedX;con.a=0;con.val=0;con.expression="zero";con.id="left";conflict.cons.push_back(con);con.val=10;con.expression="width";con.id="right";conflict.cons.push_back(con);
        const auto source=sketchToJson(conflict);auto report=proposeSketchConstraintRepairs(conflict);
        check(sketchToJson(conflict)==source,"Repair generation preserves exact source, expressions, coordinates and constraints");
        check(field(report,"status").str=="review_required"&&field(report,"proposals").arr.size()==2,"Both single-deletion alternatives are visible for explicit selection");
        for(const auto& proposal:field(report,"proposals").arr)check(field(proposal,"candidate_residual").num<=1e-6&&field(proposal,"deletion_count").num==1,"Each proposed deletion has measured local convergence");
        con.val=20;con.id="third";conflict.cons.push_back(con);
        report=proposeSketchConstraintRepairs(conflict);check(field(report,"status").str=="no_single_deletion_repair"&&field(report,"proposals").arr.empty(),"Multiple-deletion conflicts disclose unsupported search without dropping relations");
        bool rejected=false;try{proposeSketchConstraintRepairs(conflict,.001);}catch(const std::exception&){rejected=true;}check(rejected,"Repair cannot relax residual tolerance");
        conflict.cons.resize(1);check(field(proposeSketchConstraintRepairs(conflict),"status").str=="already_satisfied","Already satisfiable source never proposes unnecessary deletion");
        conflict.cons.resize(129,con);check(field(proposeSketchConstraintRepairs(conflict),"status").str=="unsupported_size","Bounded repair search explicitly rejects oversized input");
    }
    std::cout<<"sketch diagnosis checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
