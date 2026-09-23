#include "ark/sketch.hpp"
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
    std::cout<<"sketch diagnosis checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
