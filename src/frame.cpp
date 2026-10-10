#include "ark/frame.hpp"
#include "ark/native_backend.hpp"

#include <cmath>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

namespace ark {
namespace {
using V = arkjson::Value;

double finiteExpression(const V& value,const ExprEnv& env,const std::string& path){
    if(value.t==V::Num){
        if(std::isfinite(value.num)&&std::abs(value.num)<=1e8)return value.num;
    }else if(value.t==V::Str&&value.str.size()<=2048){
        bool ok=false;const double number=evalExpr(value.str,env,&ok);
        if(ok&&std::isfinite(number)&&std::abs(number)<=1e8)return number;
    }
    throw std::runtime_error("Invalid construction frame expression at "+path);
}
const V& field(const V& object,const char* key){
    const V* value=object.find(key);
    if(!value)throw std::runtime_error(std::string("Construction frame needs ")+key);
    return *value;
}
std::string name(const V& object,const char* key){
    const V& value=field(object,key);
    if(value.t!=V::Str||value.str.empty()||value.str.size()>128)
        throw std::runtime_error(std::string("Invalid construction frame ")+key);
    return value.str;
}
V vector3(const V& value,const ExprEnv& env,const std::string& path){
    if(value.t!=V::Arr||value.arr.size()!=3)
        throw std::runtime_error(path+" must be a three-coordinate vector");
    V out=V::makeArr();
    for(size_t i=0;i<3;++i)
        out.arr.push_back(V::makeNum(finiteExpression(value.arr[i],env,path+"["+std::to_string(i)+"]")));
    return out;
}
V number(const V& object,const char* key,const ExprEnv& env,const std::string& path){
    return V::makeNum(finiteExpression(field(object,key),env,path+"."+key));
}
void namesUnique(const V& rows,const char* fieldName,size_t limit){
    if(rows.t!=V::Arr||rows.arr.empty()||rows.arr.size()>limit)
        throw std::runtime_error(std::string("Construction frame ")+fieldName+" count is outside 1.."+std::to_string(limit));
    std::set<std::string> seen;
    for(const V& row:rows.arr){
        if(row.t!=V::Obj)throw std::runtime_error(std::string("Construction frame ")+fieldName+" entry must be an object");
        if(!seen.insert(name(row,"name")).second)
            throw std::runtime_error(std::string("Duplicate construction frame ")+fieldName+" name");
    }
}
} // namespace

Body evaluateFrameFeature(const Feature& feature,const ExprEnv& env,
                          Document& document,std::vector<Body>& preceding){
    (void)preceding;
    if(!feature.inputs.empty())throw std::runtime_error("Construction frame does not take feature inputs");
    const V& program=feature.nativeSpec;
    if(program.t!=V::Obj)throw std::runtime_error("Construction frame native program must be an object");
    const V& authoredProfiles=field(program,"profiles");
    const V& authoredNodes=field(program,"nodes");
    const V& authoredMembers=field(program,"members");
    namesUnique(authoredProfiles,"profiles",32);
    namesUnique(authoredNodes,"nodes",128);
    namesUnique(authoredMembers,"members",64);

    V request=V::makeObj(),profiles=V::makeArr(),nodes=V::makeArr(),members=V::makeArr();
    for(const V& profile:authoredProfiles.arr){
        const std::string section=name(profile,"section");
        if(section!="round"&&section!="rect")throw std::runtime_error("Frame profile section must be round or rect");
        V out=V::makeObj();out.set("name",V::makeStr(name(profile,"name")));
        out.set("section",V::makeStr(section));
        if(section=="round")out.set("od",number(profile,"od",env,"profile"));
        else{out.set("width",number(profile,"width",env,"profile"));out.set("height",number(profile,"height",env,"profile"));}
        out.set("wall",number(profile,"wall",env,"profile"));
        if(const V* material=profile.find("material")){
            if(profile.find("density_g_cm3"))throw std::runtime_error("Frame profile chooses material or density, not both");
            if(material->t!=V::Str||material->str.empty())throw std::runtime_error("Frame profile material must be a name");
            bool found=false;
            for(const Material& candidate:document.materials)if(candidate.name==material->str){
                out.set("material",*material);
                if(candidate.densityGcm3>0)out.set("density_g_cm3",V::makeNum(candidate.densityGcm3));
                found=true;break;
            }
            if(!found)throw std::runtime_error("Frame profile material not found: "+material->str);
        }else if(const V* density=profile.find("density_g_cm3"))
            out.set("density_g_cm3",V::makeNum(finiteExpression(*density,env,"profile.density_g_cm3")));
        profiles.arr.push_back(std::move(out));
    }
    for(const V& node:authoredNodes.arr){
        V out=V::makeObj();out.set("name",V::makeStr(name(node,"name")));
        out.set("xyz",vector3(field(node,"xyz"),env,"node.xyz"));
        nodes.arr.push_back(std::move(out));
    }
    for(const V& member:authoredMembers.arr){
        V out=V::makeObj();
        for(const char* key:{"name","profile","start"})out.set(key,V::makeStr(name(member,key)));
        if(const V* end=member.find("end"))out.set("end",V::makeStr(name(member,"end")));
        for(const char* key:{"direction","normal"})
            if(const V* value=member.find(key))out.set(key,vector3(*value,env,std::string("member.")+key));
        if(const V* authoredSegments=member.find("segments")){
            if(authoredSegments->t!=V::Arr||authoredSegments->arr.empty()||authoredSegments->arr.size()>64)
                throw std::runtime_error("Bent frame member needs 1..64 segments");
            V segments=V::makeArr();
            for(const V& segment:authoredSegments->arr){
                const std::string kind=name(segment,"kind");
                V row=V::makeObj();row.set("kind",V::makeStr(kind));
                if(kind=="line")row.set("length",number(segment,"length",env,"member.segment"));
                else if(kind=="arc"){
                    row.set("radius",number(segment,"radius",env,"member.segment"));
                    row.set("angle",number(segment,"angle",env,"member.segment"));
                    if(const V* roll=segment.find("roll"))row.set("roll",V::makeNum(finiteExpression(*roll,env,"member.segment.roll")));
                }else throw std::runtime_error("Frame segment kind must be line or arc");
                segments.arr.push_back(std::move(row));
            }
            out.set("segments",std::move(segments));
        }
        members.arr.push_back(std::move(out));
    }
    request.set("profiles",std::move(profiles));request.set("nodes",std::move(nodes));request.set("members",std::move(members));
    if(const V* joints=program.find("joints")){
        if(joints->t!=V::Arr||joints->arr.size()>64)throw std::runtime_error("Frame joints need 0..64 entries");V evaluated=V::makeArr();
        for(const auto& joint:joints->arr){const auto kind=name(joint,"kind");std::set<std::string> allowed{"kind","member","end"};if(kind=="cope"||kind=="trim_extend")allowed.insert("against");if(kind=="trim_extend")allowed.insert("max_extension");if(kind=="trim_plane"){allowed.insert("origin");allowed.insert("normal");}for(const auto& item:joint.obj)if(!allowed.count(item.first))throw std::runtime_error("Unsupported frame joint field: "+item.first);V row=joint;for(const char* key:{"max_extension"})if(const auto* value=joint.find(key))row.set(key,V::makeNum(finiteExpression(*value,env,std::string("joint.")+key)));for(const char* key:{"origin","normal"})if(const auto* value=joint.find(key))row.set(key,vector3(*value,env,std::string("joint.")+key));evaluated.arr.push_back(row);}request.set("joints",evaluated);
    }
    if(const V* welds=program.find("welds")){
        if(welds->t!=V::Arr||welds->arr.size()>64)throw std::runtime_error("Frame welds need 0..64 entries");V evaluated=V::makeArr();std::set<std::string> ids;
        for(const auto& weld:welds->arr){const std::set<std::string> allowed{"id","member_a","member_b","path","normal","leg_a","leg_b","material","density_g_cm3"};for(const auto& item:weld.obj)if(!allowed.count(item.first))throw std::runtime_error("Unsupported frame weld field: "+item.first);V row=V::makeObj();for(const char* key:{"id","member_a","member_b"})row.set(key,V::makeStr(name(weld,key)));if(!ids.insert(name(weld,"id")).second)throw std::runtime_error("Duplicate frame weld id");const auto& path=field(weld,"path");if(path.t!=V::Arr||path.arr.size()!=2)throw std::runtime_error("Weld path supports exactly two authored endpoints");V points=V::makeArr();for(const auto& point:path.arr)points.arr.push_back(vector3(point,env,"weld.path"));row.set("path",points);row.set("normal",vector3(field(weld,"normal"),env,"weld.normal"));for(const char* key:{"leg_a","leg_b"})row.set(key,number(weld,key,env,"weld"));
            if(const auto* material=weld.find("material")){if(weld.find("density_g_cm3"))throw std::runtime_error("Weld chooses material or density, not both");bool found=false;for(const auto& candidate:document.materials)if(candidate.name==material->asStr()){row.set("material",*material);if(candidate.densityGcm3>0)row.set("density_g_cm3",V::makeNum(candidate.densityGcm3));found=true;break;}if(!found)throw std::runtime_error("Weld material not found");}else if(const auto* density=weld.find("density_g_cm3"))row.set("density_g_cm3",V::makeNum(finiteExpression(*density,env,"weld.density")));evaluated.arr.push_back(row);
        }request.set("welds",evaluated);
    }
    V envelope=V::makeObj();envelope.set("operation",V::makeStr("model_frame"));envelope.set("request",std::move(request));
    Body body;body.nativeData=nativeWorkerRequest(envelope);
    body.baked=std::make_unique<Mesh>(nativeResultMesh(body.nativeData));
    return body;
}

V applyFrameCommand(Document& document,const V& command){
    const std::string op=name(command,"op");const auto& selected=field(command,"id");if(selected.t!=V::Num||selected.num<1||selected.num!=std::floor(selected.num))throw std::runtime_error("Frame command needs a feature ID");auto* feature=document.find((u64)selected.num);if(!feature||feature->type!=FeatureType::ConstructionFrame)throw std::runtime_error("Select a construction_frame feature");
    const bool weld=op.rfind("frame.weld.",0)==0;const char* key=weld?"welds":"joints";V list=feature->nativeSpec.find(key)?*feature->nativeSpec.find(key):V::makeArr();if(list.t!=V::Arr)throw std::runtime_error("Frame entities must be an array");V result=V::makeObj();result.set("feature_id",selected);
    if(op=="frame.trim_extend"||op=="frame.trim_plane"){
        V joint=field(command,"joint");if(joint.t!=V::Obj)throw std::runtime_error("Frame joint must be an object");joint.set("kind",V::makeStr(op=="frame.trim_extend"?"trim_extend":"trim_plane"));const auto member=name(joint,"member"),end=name(joint,"end");if(end!="start"&&end!="end")throw std::runtime_error("Frame joint end must be start or end");for(const auto& existing:list.arr)if(name(existing,"member")==member&&name(existing,"end")==end)throw std::runtime_error("Frame member endpoint already has a joint; remove it explicitly first");list.arr.push_back(joint);result.set("entity_id",V::makeStr(member+"."+end));
    }else if(op=="frame.joint.remove"){
        const auto member=name(command,"member"),end=name(command,"end");const auto old=list.arr.size();list.arr.erase(std::remove_if(list.arr.begin(),list.arr.end(),[&](const V& row){return name(row,"member")==member&&name(row,"end")==end;}),list.arr.end());if(old==list.arr.size())throw std::runtime_error("Frame endpoint joint not found");result.set("entity_id",V::makeStr(member+"."+end));
    }else if(weld){
        const auto action=op.substr(op.find_last_of('.')+1);if(action=="add"){V row=field(command,"weld");const auto id=name(row,"id");for(const auto& old:list.arr)if(name(old,"id")==id)throw std::runtime_error("Duplicate frame weld id");list.arr.push_back(row);result.set("entity_id",V::makeStr(id));}
        else{const auto id=name(command,"entity");auto found=std::find_if(list.arr.begin(),list.arr.end(),[&](const V& row){return name(row,"id")==id;});if(found==list.arr.end())throw std::runtime_error("Frame weld not found");if(action=="remove")list.arr.erase(found);else if(action=="edit"){const auto& values=field(command,"values");if(values.t!=V::Obj||values.find("id"))throw std::runtime_error("Weld edit values must preserve id");for(const auto& item:values.obj)found->set(item.first,item.second);}else throw std::runtime_error("Unknown frame weld operation");result.set("entity_id",V::makeStr(id));}
    }else throw std::runtime_error("Unknown frame command");if(list.arr.size()>64)throw std::runtime_error("Frame accepts at most 64 joints/welds");feature->nativeSpec.set(key,list);return result;
}

} // namespace ark
