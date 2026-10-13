#include "ark/collision.hpp"
#include "ark/mesh_collision.hpp"
#include "ark/native_features.hpp"
#include <cmath>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
struct Operand {u64 feature=0,node=0;int copy=0;Mat4 transform=Mat4::identity();};
u64 featureId(const Document& d,const V& value){u64 selected=0;for(const auto& f:d.features)if((value.t==V::Str&&f.name==value.str)||(value.t==V::Num&&value.num==(double)f.id)){if(selected)throw std::runtime_error("Ambiguous collision feature");selected=f.id;}if(!selected)throw std::runtime_error("Collision feature not found");return selected;}
Operand operand(const Document& d,const V& value,const std::vector<AssemblyPlacement>& placements){
    Operand out;if(value.t==V::Num||value.t==V::Str){out.feature=featureId(d,value);return out;}
    if(value.t!=V::Obj)throw std::runtime_error("Collision operand needs a feature id/name or {id} / {node,copy}");
    for(const auto& entry:value.obj)if(entry.first!="id"&&entry.first!="node"&&entry.first!="copy")throw std::runtime_error("Unknown collision operand field: "+entry.first);
    if(value.find("id")&&value.find("node"))throw std::runtime_error("Select collision id or node, not both");
    if(const auto* id=value.find("id")){if(value.find("copy"))throw std::runtime_error("Collision copy requires node");out.feature=featureId(d,*id);return out;}
    const auto* node=value.find("node");if(!node||(node->t!=V::Num&&node->t!=V::Str))throw std::runtime_error("Collision occurrence requires node id/name");
    for(const auto& n:d.assembly)if((node->t==V::Str&&n.name==node->str)||(node->t==V::Num&&node->num==(double)n.id)){if(out.node)throw std::runtime_error("Ambiguous collision occurrence");out.node=n.id;}
    if(!out.node)throw std::runtime_error("Collision occurrence not found");
    if(const auto* copy=value.find("copy")){if(copy->t!=V::Num||copy->num<0||copy->num>31||copy->num!=std::floor(copy->num))throw std::runtime_error("Collision copy must be integer 0..31");out.copy=(int)copy->num;}
    for(const auto& placement:placements)if(placement.node==out.node&&placement.copy==out.copy){out.feature=placement.feature;out.transform=placement.world;return out;}
    throw std::runtime_error("Collision occurrence/copy is not placed");
}
const Body& body(const Document& d,u64 id){for(const auto& b:d.evalBodies)if(b.id==id)return b;throw std::runtime_error("Collision operand has no evaluated geometry");}
V identity(const Operand& operand){V out=V::makeObj();out.set("feature_id",V::makeNum((double)operand.feature));if(operand.node){out.set("node",V::makeNum((double)operand.node));out.set("copy",V::makeNum(operand.copy));}return out;}
}
arkjson::Value inspectDocumentCollision(Document& d,const V& args){
    for(const auto& entry:args.obj)if(entry.first!="a"&&entry.first!="b"&&entry.first!="mode"&&entry.first!="expected_revision")throw std::runtime_error("Unknown collision argument: "+entry.first);
    const auto* av=args.find("a"),*bv=args.find("b");if(!av||!bv)throw std::runtime_error("Collision query requires a and b");
    std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(d,placements,error))throw std::runtime_error(error);
    const Operand a=operand(d,*av,placements),b=operand(d,*bv,placements);
    if(a.feature==b.feature&&a.node==b.node&&a.copy==b.copy)throw std::runtime_error("Select distinct collision operands or distinct occurrences");
    std::string mode="auto";if(const auto* value=args.find("mode")){if(value->t!=V::Str||(value->str!="auto"&&value->str!="exact"&&value->str!="mesh"))throw std::runtime_error("Collision mode must be auto, exact or mesh");mode=value->str;}
    if(mode=="exact"){
        if(isMeshModelFeature(d.find(a.feature)->type)||isMeshModelFeature(d.find(b.feature)->type))throw std::runtime_error("Exact collision cannot reinterpret mesh features as solids");
        ensureNativeBody(d,a.feature,d.evalBodies);ensureNativeBody(d,b.feature,d.evalBodies);
    }
    const auto& ba=body(d,a.feature);const auto& bb=body(d,b.feature);
    const bool nativeA=ba.nativeData.find("brep_base64"),nativeB=bb.nativeData.find("brep_base64");
    V report;
    if(mode=="exact"||(mode=="auto"&&nativeA&&nativeB))report=inspectNativeCollision(ba,a.transform,bb,b.transform);
    else{const Mesh ma=ba.baked&&ba.baked->triCount()?*ba.baked:ba.tessellate(d.tessOpt);const Mesh mb=bb.baked&&bb.baked->triCount()?*bb.baked:bb.tessellate(d.tessOpt);report=inspectMeshCollision(ma,a.transform,mb,b.transform);report.set("sampled_from_brep_a",V::makeBool(nativeA));report.set("sampled_from_brep_b",V::makeBool(nativeB));}
    report.set("a",identity(a));report.set("b",identity(b));report.set("requested_mode",V::makeStr(mode));report.set("physics_simulation",V::makeBool(false));report.set("document_unchanged",V::makeBool(true));return report;
}
}
