#include "ark/assembly_io.hpp"
#include <stdexcept>
namespace ark {
using V=arkjson::Value;
V mateToJson(const AssemblyMate& m) {
    V j=V::makeObj();j.set("id",V::makeNum((double)m.id));j.set("a",V::makeNum((double)m.a));j.set("b",V::makeNum((double)m.b));
    j.set("type",V::makeStr(m.type));j.set("reference_a",V::makeStr(m.reference_a));j.set("reference_b",V::makeStr(m.reference_b));
    j.set("value",V::makeStr(m.value));j.set("fraction_a",V::makeNum(m.fraction_a));j.set("fraction_b",V::makeNum(m.fraction_b));if(!m.motion.empty()){V fields=V::makeObj();for(const auto& item:m.motion)fields.set(item.first,V::makeStr(item.second));j.set("motion",fields);}
    if(m.limits.enabled){V limits=V::makeObj();limits.set("min",V::makeStr(m.limits.min));limits.set("max",V::makeStr(m.limits.max));j.set("limits",limits);}return j;
}
void editMate(AssemblyMate& m,const V& j) {
    if(j.t!=V::Obj)throw std::runtime_error("Mate must be an object");
    for(auto kv:{std::pair{"a",&m.a},std::pair{"b",&m.b}})if(auto p=j.find(kv.first)){
        if(p->t!=V::Num||!std::isfinite(p->num)||p->num<1||p->num>9007199254740990.||p->num!=std::floor(p->num))throw std::runtime_error("Mate endpoints must be instance IDs");*kv.second=(u64)p->num;
    }
    for(auto kv:{std::pair{"type",&m.type},std::pair{"reference_a",&m.reference_a},std::pair{"reference_b",&m.reference_b}})if(auto p=j.find(kv.first)){
        if(p->t!=V::Str||p->str.empty()||p->str.size()>128)throw std::runtime_error("Mate reference/type needs 1..128 characters");*kv.second=p->str;
    }
    if(auto p=j.find("value")){if(p->t==V::Num&&std::isfinite(p->num))m.value=p->dump();else if(p->t==V::Str&&!p->str.empty()&&p->str.size()<=2048)m.value=p->str;else throw std::runtime_error("Mate value must be finite or expression");}
    for(auto kv:{std::pair{"fraction_a",&m.fraction_a},std::pair{"fraction_b",&m.fraction_b}})if(auto p=j.find(kv.first)){
        if(p->t!=V::Num||!std::isfinite(p->num)||p->num<0||p->num>1)throw std::runtime_error("Mate curve fraction must be in [0,1]");*kv.second=p->num;
    }
    if(const auto* motion=j.find("motion")){if(motion->t!=V::Obj)throw std::runtime_error("Mechanical mate motion must be an object");m.motion.clear();for(const auto& item:motion->obj){if(item.second.t==V::Num&&std::isfinite(item.second.num))m.motion[item.first]=item.second.dump();else if(item.second.t==V::Str&&!item.second.str.empty()&&item.second.str.size()<=2048)m.motion[item.first]=item.second.str;else throw std::runtime_error("Mechanical motion field must be finite or an expression");}}
    if(m.type=="slider")m.type="prismatic";
    if(const auto* limits=j.find("limits")){
        if(limits->t==V::Null)m.limits={};
        else{
            if(limits->t!=V::Obj||limits->obj.size()!=2||!limits->find("min")||!limits->find("max"))throw std::runtime_error("ASSEMBLY_INVALID_JOINT_LIMITS: limits requires min and max");
            for(auto field:{std::pair{"min",&m.limits.min},std::pair{"max",&m.limits.max}}){const auto* value=limits->find(field.first);
                if(value->t==V::Num&&std::isfinite(value->num))*field.second=value->dump();
                else if(value->t==V::Str&&!value->str.empty()&&value->str.size()<=2048)*field.second=value->str;
                else throw std::runtime_error("ASSEMBLY_INVALID_JOINT_LIMITS: min and max must be finite or expressions");}
            m.limits.enabled=true;
        }
    }
    if(!m.a||!m.b||m.a==m.b)throw std::runtime_error("Mate needs two distinct instances");
    if(m.type!="concentric"&&m.type!="coincident"&&m.type!="revolute"&&m.type!="prismatic"&&m.type!="rigid"&&m.type!="contact"&&m.type!="distance"&&m.type!="angle"&&!isMechanicalMate(m.type))throw std::runtime_error("Unknown mate type");
    if(!isMechanicalMate(m.type)&&!m.motion.empty())throw std::runtime_error("motion fields require a mechanical mate type");
    if(m.limits.enabled&&m.type!="revolute"&&m.type!="prismatic")throw std::runtime_error("ASSEMBLY_INVALID_JOINT_LIMITS: limits require revolute or prismatic");
}
V assemblyDiagnosticsJson(const AssemblyDiagnostics& d) {
    V j=V::makeObj(),rows=V::makeArr(),unconstrained=V::makeArr();j.set("jacobian_rank",V::makeNum(d.rank));j.set("remaining_dofs",V::makeNum(d.freeDofs));
    j.set("max_position_residual_mm",V::makeNum(d.maxPositionMm));j.set("max_angle_residual_deg",V::makeNum(d.maxAngleDeg));
    for(auto& m:d.mates){V row=V::makeObj();row.set("id",V::makeNum((double)m.id));row.set("type",V::makeStr(m.type));row.set("position_residual_mm",V::makeNum(m.positionMm));row.set("angle_residual_deg",V::makeNum(m.angleDeg));row.set("value_error",V::makeNum(m.valueError));
        if(m.hasCoordinate){row.set("position",V::makeNum(m.coordinate));row.set("unit",V::makeStr(m.type=="revolute"?"deg":"mm"));}
        if(m.limited){V limits=V::makeObj();limits.set("min",V::makeNum(m.min));limits.set("max",V::makeNum(m.max));row.set("limits",limits);row.set("limit_residual",V::makeNum(m.limitError));}rows.arr.push_back(row);}
    for(auto id:d.unconstrainedNodes)unconstrained.arr.push_back(V::makeNum((double)id));j.set("mates",rows);j.set("unconstrained_nodes",unconstrained);j.set("rank_scope",V::makeStr("local numerical geometric equality constraint Jacobian; joint stop inequalities excluded"));return j;
}
}
