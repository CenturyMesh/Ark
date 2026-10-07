#include "ark/occt_frame.hpp"
#include "ark/occt_solid.hpp"
#include "common.hpp"

#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ark::occt {
namespace {
using V=arkjson::Value;

const V& required(const V& object,const char* key){
    const V* value=object.find(key);
    if(!value)throw std::runtime_error(std::string("Construction frame needs ")+key);
    return *value;
}
std::string word(const V& object,const char* key){
    const V& value=required(object,key);
    if(value.t!=V::Str||value.str.empty()||value.str.size()>128)
        throw std::runtime_error(std::string("Invalid construction frame ")+key);
    return value.str;
}
double scalar(const V& object,const char* key){
    const V& value=required(object,key);
    if(value.t!=V::Num||!std::isfinite(value.num)||std::abs(value.num)>1e8)
        throw std::runtime_error(std::string("Invalid construction frame number: ")+key);
    return value.num;
}
double positive(const V& object,const char* key){
    const double value=scalar(object,key);
    if(value<=0)throw std::runtime_error(std::string("Frame ")+key+" must be positive");
    return value;
}
gp_Pnt point(const V& coordinates,const char* label){
    if(coordinates.t!=V::Arr||coordinates.arr.size()!=3)throw std::runtime_error(std::string(label)+" must have three coordinates");
    for(const V& value:coordinates.arr)if(value.t!=V::Num||!std::isfinite(value.num)||std::abs(value.num)>1e8)
        throw std::runtime_error(std::string(label)+" has a non-finite coordinate");
    return {coordinates.arr[0].num,coordinates.arr[1].num,coordinates.arr[2].num};
}
V xyz(const gp_Pnt& p){return vec(p.XYZ());}
V direction(const gp_Dir& d){return vec(d.XYZ());}
V frame(const gp_Pnt& origin,const gp_Dir& x,const gp_Dir& y){
    const gp_Dir z(x.Crossed(y));V result=V::makeArr();
    for(double value:{x.X(),x.Y(),x.Z(),0.,y.X(),y.Y(),y.Z(),0.,z.X(),z.Y(),z.Z(),0.,origin.X(),origin.Y(),origin.Z(),1.})
        result.arr.push_back(V::makeNum(value));
    return result;
}
gp_Dir projectedNormal(const gp_Dir& tangent,const V* authored){
    gp_Vec normal;
    if(authored){
        const gp_Pnt value=point(*authored,"Member normal");
        normal=gp_Vec(value.X(),value.Y(),value.Z());
        if(normal.Magnitude()<1e-10)throw std::runtime_error("Member normal is zero");
    }else normal=std::abs(tangent.Dot(gp_Dir(0,0,1)))<0.9?gp_Vec(0,0,1):gp_Vec(0,1,0);
    normal-=gp_Vec(tangent)*normal.Dot(gp_Vec(tangent));
    if(normal.Magnitude()<1e-8)throw std::runtime_error("Member normal is parallel to its tangent");
    return gp_Dir(normal);
}
gp_Trsf rigid(const V& f){
    const auto& a=f.arr;gp_Trsf t;
    t.SetValues(a[0].num,a[4].num,a[8].num,a[12].num,
                a[1].num,a[5].num,a[9].num,a[13].num,
                a[2].num,a[6].num,a[10].num,a[14].num);
    return t;
}
struct Profile{
    std::string name,section,material;double od=0,width=0,height=0,wall=0,density=0,area=0;
};
struct Member{
    std::string name,profile,startNode,endNode;bool bent=false;
    gp_Pnt start,end;gp_Dir startTangent{1,0,0},endTangent{1,0,0},startNormal{0,1,0},endNormal{0,1,0};
    double developedLength=0;V result;
};
Profile profileOf(const V& row){
    Profile p;p.name=word(row,"name");p.section=word(row,"section");p.wall=positive(row,"wall");
    if(p.section=="round"){
        p.od=positive(row,"od");if(p.od<=2*p.wall)throw std::runtime_error("Frame round tube wall leaves no opening");
        const double inner=p.od-2*p.wall;p.area=std::acos(-1.0)*(p.od*p.od-inner*inner)/4;
    }else if(p.section=="rect"){
        p.width=positive(row,"width");p.height=positive(row,"height");
        if(p.width<=2*p.wall||p.height<=2*p.wall)throw std::runtime_error("Frame rectangular tube wall leaves no opening");
        p.area=p.width*p.height-(p.width-2*p.wall)*(p.height-2*p.wall);
    }else throw std::runtime_error("Frame profile must be round or rect");
    if(const V* material=row.find("material"))p.material=word(row,"material");
    if(row.find("density_g_cm3")){
        p.density=scalar(row,"density_g_cm3");
        if(p.density<0||p.density>100)throw std::runtime_error("Frame density must be 0..100 g/cm3");
    }
    return p;
}
const V& rows(const V& request,const char* key,size_t maximum){
    const V& result=required(request,key);
    if(result.t!=V::Arr||result.arr.empty()||result.arr.size()>maximum)
        throw std::runtime_error(std::string("Frame ")+key+" needs 1.."+std::to_string(maximum)+" entries");
    return result;
}
V endpoint(const gp_Pnt& at,const gp_Dir& tangent,const gp_Dir& normal){
    V result=V::makeObj();result.set("point_mm",xyz(at));result.set("tangent",direction(tangent));
    result.set("normal",direction(normal));result.set("frame",frame(at,tangent,normal));return result;
}
void advance(Member& member,const V& segments){
    gp_Pnt at(0,0,0);gp_Dir tangent(1,0,0),normal(0,1,0),bendNormal=normal;
    const double pi=std::acos(-1.0);
    for(const V& segment:segments.arr){
        const std::string kind=word(segment,"kind");
        if(kind=="line"){
            const double length=positive(segment,"length");at.Translate(gp_Vec(tangent)*length);member.developedLength+=length;
        }else if(kind=="arc"){
            const double radius=positive(segment,"radius"),degrees=scalar(segment,"angle");
            const double roll=segment.find("roll")?scalar(segment,"roll"):0;
            if(std::abs(degrees)<.001||std::abs(degrees)>180||std::abs(roll)>360)
                throw std::runtime_error("Frame bend angle must be in [-180,180] excluding zero, roll in [-360,360]");
            gp_Trsf twist;twist.SetRotation(gp_Ax1(at,tangent),roll*pi/180);
            bendNormal=gp_Dir(gp_Vec(bendNormal).Transformed(twist));
            const gp_Dir inward=degrees<0?bendNormal.Reversed():bendNormal;
            const gp_Dir axis(tangent.Crossed(inward));
            const gp_Pnt center=at.Translated(gp_Vec(inward)*radius);
            const double angle=std::abs(degrees)*pi/180;
            gp_Trsf swing;swing.SetRotation(gp_Ax1(center,axis),angle);
            at.Transform(swing);tangent=gp_Dir(gp_Vec(tangent).Transformed(swing));
            normal=gp_Dir(gp_Vec(normal).Transformed(swing));
            bendNormal=gp_Dir(gp_Vec(bendNormal).Transformed(swing));
            member.developedLength+=radius*angle;
        }else throw std::runtime_error("Frame segment kind must be line or arc");
    }
    V placement=frame(member.start,member.startTangent,member.startNormal);
    gp_Trsf transform=rigid(placement);
    at.Transform(transform);
    member.end=at;
    member.endTangent=gp_Dir(gp_Vec(tangent).Transformed(transform));
    member.endNormal=gp_Dir(gp_Vec(normal).Transformed(transform));
}
Member memberOf(const V& row,const std::map<std::string,Profile>& profiles,const std::map<std::string,gp_Pnt>& nodes){
    Member m;m.name=word(row,"name");m.profile=word(row,"profile");m.startNode=word(row,"start");
    if(!profiles.count(m.profile))throw std::runtime_error("Frame member "+m.name+" has unknown profile");
    const auto start=nodes.find(m.startNode);
    if(start==nodes.end())throw std::runtime_error("Frame member "+m.name+" has unknown start node");
    m.start=start->second;
    if(const V* end=row.find("end"))m.endNode=word(row,"end");
    const V* segments=row.find("segments");m.bent=segments!=nullptr;
    if(!m.bent){
        if(row.find("direction"))throw std::runtime_error("Straight frame member uses its two nodes for direction");
        if(m.endNode.empty())throw std::runtime_error("Straight frame member needs an end node");
        const auto end=nodes.find(m.endNode);
        if(end==nodes.end())throw std::runtime_error("Frame member "+m.name+" has unknown end node");
        m.end=end->second;
        gp_Vec chord(m.start,m.end);m.developedLength=chord.Magnitude();
        if(m.developedLength<=1e-6)throw std::runtime_error("Frame member has coincident endpoints");
        m.startTangent=m.endTangent=gp_Dir(chord);
        m.startNormal=m.endNormal=projectedNormal(m.startTangent,row.find("normal"));
    }else{
        if(segments->t!=V::Arr||segments->arr.empty()||segments->arr.size()>64)
            throw std::runtime_error("Bent frame member needs 1..64 segments");
        const V* directionValue=row.find("direction");
        if(!directionValue)throw std::runtime_error("Bent frame member needs a start direction");
        const gp_Pnt vector=point(*directionValue,"Member direction");
        gp_Vec axis(vector.X(),vector.Y(),vector.Z());
        if(axis.Magnitude()<1e-9)throw std::runtime_error("Bent frame direction is zero");
        m.startTangent=gp_Dir(axis);m.startNormal=projectedNormal(m.startTangent,row.find("normal"));
        const auto& p=profiles.at(m.profile);
        const double envelope=p.section=="round"?p.od/2:std::hypot(p.width,p.height)/2;
        for(const V& segment:segments->arr)if(word(segment,"kind")=="arc"&&positive(segment,"radius")<=envelope)
            throw std::runtime_error("Frame bend radius must exceed the profile envelope");
        advance(m,*segments);
        if(!m.endNode.empty()){
            const auto end=nodes.find(m.endNode);
            if(end==nodes.end())throw std::runtime_error("Frame member "+m.name+" has unknown end node");
            if(m.end.Distance(end->second)>1e-4)
                throw std::runtime_error("Frame bent member endpoint does not match named node");
        }
    }
    return m;
}
V makeTube(const Member& member,const Profile& profile,const V& authored){
    V request=V::makeObj(),segments=V::makeArr();
    request.set("op",V::makeStr(profile.section=="round"?"round_tube":"rect_tube"));
    if(profile.section=="round")request.set("od",V::makeNum(profile.od));
    else{request.set("width",V::makeNum(profile.width));request.set("height",V::makeNum(profile.height));}
    request.set("wall",V::makeNum(profile.wall));
    if(const V* bends=authored.find("segments"))segments=*bends;
    else{V line=V::makeObj();line.set("kind",V::makeStr("line"));line.set("length",V::makeNum(member.developedLength));segments.arr.push_back(std::move(line));}
    request.set("segments",std::move(segments));
    V local=modelSolidNative(request);
    V move=V::makeObj();move.set("op",V::makeStr("transform"));
    move.set("input_brep",required(local,"brep_base64"));move.set("input_topology",required(local,"topology"));
    move.set("frame",frame(member.start,member.startTangent,member.startNormal));
    return modelSolidNative(move);
}
void cope(Member& member,const Member& host,const Profile& ownProfile,const Profile& hostProfile,const std::string& end){
    if(ownProfile.section!="round"||hostProfile.section!="round"||host.bent)
        throw std::runtime_error("Exact frame cope supports a round member against a straight round host");
    if(member.name==host.name)throw std::runtime_error("Frame member cannot cope against itself");
    const gp_Pnt endpoint=end=="start"?member.start:member.end;
    const gp_Dir axis(host.startTangent);const gp_Vec delta(host.start,endpoint);
    const double station=delta.Dot(gp_Vec(axis));
    const gp_Vec radial=delta-gp_Vec(axis)*station;
    if(radial.Magnitude()>1e-4||station<=hostProfile.od/2+1e-4||station>=host.developedLength-hostProfile.od/2-1e-4)
        throw std::runtime_error("Frame cope endpoint must lie on the host centerline, clear of its ends");
    TopoDS_Shape envelope=BRepPrimAPI_MakeCylinder(gp_Ax2(host.start,axis),hostProfile.od/2,host.developedLength).Shape();
    V cut=V::makeObj();cut.set("op",V::makeStr("cut"));
    cut.set("input_brep",required(member.result,"brep_base64"));
    cut.set("input_topology",required(member.result,"topology"));
    cut.set("tool_brep",V::makeStr(brepData(envelope)));
    const double before=required(required(member.result,"report"),"volume_mm3").num;
    V outcome=modelSolidNative(cut);
    const double after=required(required(outcome,"report"),"volume_mm3").num;
    if(after<=1e-8||before-after<=1e-8||required(required(outcome,"report"),"solids").num!=1)
        throw std::runtime_error("Frame cope did not leave one positively trimmed solid");
    member.result=std::move(outcome);
}
int shapeIndex(const TopoDS_Shape& parent,const TopoDS_Shape& part,TopAbs_ShapeEnum kind){
    int index=0;for(const auto& candidate:shapes(parent,kind)){if(candidate.IsSame(part))return index;++index;}return -1;
}
V topologyFor(const TopoDS_Shape& combined,const std::string& encoded,
              const std::vector<Member>& members,const std::vector<TopoDS_Shape>& memberShapes){
    V result=V::makeObj(),references=V::makeArr();
    result.set("brep_sha256",V::makeStr(sha256(base64Decode(encoded))));
    for(size_t memberNumber=0;memberNumber<members.size();++memberNumber){
        const Member& member=members[memberNumber];
        const TopoDS_Shape& part=memberShapes[memberNumber];
        const V& source=required(required(member.result,"topology"),"references");
        if(source.t!=V::Arr)throw std::runtime_error("Frame member topology is not an array");
        for(const V& reference:source.arr){
            V row=reference;row.set("name",V::makeStr("member."+member.name+"."+word(reference,"name")));
            const std::string status=word(reference,"status"),kind=word(reference,"kind");
            if(status=="bound"||status=="merged"){
                const V& sourceIndices=required(reference,"indices");
                const TopAbs_ShapeEnum type=kind=="face"||kind=="axis"?TopAbs_FACE:kind=="edge"?TopAbs_EDGE:TopAbs_SHAPE;
                if(type==TopAbs_SHAPE||sourceIndices.t!=V::Arr||sourceIndices.arr.size()!=1||sourceIndices.arr[0].t!=V::Num)
                    throw std::runtime_error("Frame member has invalid semantic reference");
                const auto parts=shapes(part,type);const auto sourceIndex=static_cast<size_t>(sourceIndices.arr[0].num);
                if(sourceIndex>=parts.size())throw std::runtime_error("Frame member reference index is stale");
                const int index=shapeIndex(combined,parts[sourceIndex],type);
                if(index<0)throw std::runtime_error("Frame member reference did not survive compound assembly");
                V indices=V::makeArr();indices.arr.push_back(V::makeNum(index));row.set("indices",std::move(indices));
            }
            references.arr.push_back(std::move(row));
        }
    }
    if(references.arr.size()>512)throw std::runtime_error("Frame has more than 512 semantic references");
    result.set("references",std::move(references));return result;
}
} // namespace

V frameModelNative(const V& request){
    const V& profileRows=rows(request,"profiles",32),nodeRows=rows(request,"nodes",128),memberRows=rows(request,"members",64);
    std::map<std::string,Profile> profiles;std::map<std::string,gp_Pnt> nodes;
    for(const V& row:profileRows.arr){Profile value=profileOf(row);if(!profiles.emplace(value.name,value).second)throw std::runtime_error("Duplicate frame profile name");}
    for(const V& row:nodeRows.arr)if(!nodes.emplace(word(row,"name"),point(required(row,"xyz"),"Frame node")).second)
        throw std::runtime_error("Duplicate frame node name");
    std::vector<Member> members;members.reserve(memberRows.arr.size());std::map<std::string,size_t> memberIndex;
    for(const V& row:memberRows.arr){
        Member member=memberOf(row,profiles,nodes);
        if(!memberIndex.emplace(member.name,members.size()).second)throw std::runtime_error("Duplicate frame member name");
        member.result=makeTube(member,profiles.at(member.profile),row);
        members.push_back(std::move(member));
    }
    V joints=V::makeArr();
    if(const V* authored=request.find("joints")){
        if(authored->t!=V::Arr||authored->arr.size()>64)throw std::runtime_error("Frame joints need 0..64 entries");
        std::set<std::pair<std::string,std::string>> applied;
        for(const V& joint:authored->arr){
            if(word(joint,"kind")!="cope")throw std::runtime_error("Frame joint kind must be cope");
            const std::string name=word(joint,"member"),end=word(joint,"end"),against=word(joint,"against");
            if(end!="start"&&end!="end")throw std::runtime_error("Frame cope end must be start or end");
            if(!applied.insert({name,end}).second)throw std::runtime_error("Frame member end has two authored copes");
            const auto member=memberIndex.find(name),host=memberIndex.find(against);
            if(member==memberIndex.end()||host==memberIndex.end())throw std::runtime_error("Frame cope names an unknown member");
            Member& target=members[member->second];const Member& support=members[host->second];
            const double before=required(required(target.result,"report"),"volume_mm3").num;
            cope(target,support,profiles.at(target.profile),profiles.at(support.profile),end);
            const double after=required(required(target.result,"report"),"volume_mm3").num;
            V evidence=V::makeObj();evidence.set("kind",V::makeStr("cope"));
            evidence.set("member",V::makeStr(name));evidence.set("end",V::makeStr(end));
            evidence.set("against",V::makeStr(against));evidence.set("removed_volume_mm3",V::makeNum(before-after));
            evidence.set("exact",V::makeBool(true));joints.arr.push_back(std::move(evidence));
        }
    }
    std::vector<TopoDS_Shape> shapesForCompound;shapesForCompound.reserve(members.size());
    V cutlist=V::makeArr(),stockRows=V::makeArr();double totalLength=0,totalMass=0,stockY=0;bool massChecked=true;
    gp_XYZ weightedCenter(0,0,0);
    for(const Member& member:members){
        const Profile& profile=profiles.at(member.profile);
        const TopoDS_Shape solid=readBrep(required(member.result,"brep_base64").str);
        shapesForCompound.push_back(solid);
        const double volume=required(required(member.result,"report"),"volume_mm3").num;
        if(!(volume>0))throw std::runtime_error("Frame member has no positive exact volume");
        GProp_GProps exactProperties;BRepGProp::VolumeProperties(solid,exactProperties);
        const gp_Pnt center=exactProperties.CentreOfMass();
        if(!std::isfinite(center.X())||!std::isfinite(center.Y())||!std::isfinite(center.Z())||
           std::abs(exactProperties.Mass()-volume)>std::max(1e-5,volume*1e-7))
            throw std::runtime_error("Frame member exact centroid or volume is invalid");
        V row=V::makeObj();row.set("name",V::makeStr(member.name));row.set("profile",V::makeStr(profile.name));
        row.set("section",V::makeStr(profile.section));row.set("start_node",V::makeStr(member.startNode));
        if(profile.section=="round")row.set("od_mm",V::makeNum(profile.od));
        else{row.set("width_mm",V::makeNum(profile.width));row.set("height_mm",V::makeNum(profile.height));}
        row.set("wall_mm",V::makeNum(profile.wall));
        if(!member.endNode.empty())row.set("end_node",V::makeStr(member.endNode));
        row.set("bent",V::makeBool(member.bent));row.set("developed_length_mm",V::makeNum(member.developedLength));
        row.set("section_area_mm2",V::makeNum(profile.area));
        row.set("nominal_blank_volume_mm3",V::makeNum(profile.area*member.developedLength));
        row.set("exact_volume_mm3",V::makeNum(volume));row.set("center_mm",xyz(center));
        row.set("start",endpoint(member.start,member.startTangent,member.startNormal));
        row.set("end",endpoint(member.end,member.endTangent,member.endNormal));
        if(!profile.material.empty())row.set("material",V::makeStr(profile.material));
        if(profile.density>0){const double mass=profile.density*volume/1000;
            row.set("density_g_cm3",V::makeNum(profile.density));row.set("mass_g",V::makeNum(mass));totalMass+=mass;
            weightedCenter+=center.XYZ()*mass;
        }else massChecked=false;
        cutlist.arr.push_back(std::move(row));totalLength+=member.developedLength;
        const double spacing=std::max(55.0,profile.section=="round"?profile.od+25:std::max(profile.width,profile.height)+25);
        V stock=V::makeObj();stock.set("name",V::makeStr(member.name));stock.set("profile",V::makeStr(profile.name));
        stock.set("section",V::makeStr(profile.section));stock.set("developed_length_mm",V::makeNum(member.developedLength));
        stock.set("row",V::makeNum(stockRows.arr.size()));
        stock.set("start_mm",vec(gp_XYZ(0,stockY,0)));stock.set("end_mm",vec(gp_XYZ(member.developedLength,stockY,0)));
        if(profile.section=="round")stock.set("od_mm",V::makeNum(profile.od));
        else{stock.set("width_mm",V::makeNum(profile.width));stock.set("height_mm",V::makeNum(profile.height));}
        stock.set("wall_mm",V::makeNum(profile.wall));stockRows.arr.push_back(std::move(stock));stockY+=spacing;
    }
    TopoDS_Shape combined=compound(shapesForCompound);
    V result=shapeResult(combined,false);
    result.set("topology",topologyFor(combined,required(result,"brep_base64").str,members,shapesForCompound));
    V report=V::makeObj(),nodeReport=V::makeArr();
    for(const V& node:nodeRows.arr){V item=V::makeObj();item.set("name",required(node,"name"));item.set("point_mm",required(node,"xyz"));nodeReport.arr.push_back(std::move(item));}
    report.set("nodes",std::move(nodeReport));report.set("members",std::move(cutlist));report.set("joints",std::move(joints));
    V stock=V::makeObj();stock.set("display_only",V::makeBool(true));
    stock.set("bend_trajectory_checked",V::makeBool(false));stock.set("members",std::move(stockRows));
    report.set("stock_preview",std::move(stock));
    report.set("member_count",V::makeNum(members.size()));report.set("total_developed_length_mm",V::makeNum(totalLength));
    report.set("mass_checked",V::makeBool(massChecked));
    if(massChecked){
        report.set("mass_g",V::makeNum(totalMass));
        report.set("center_mm",vec(weightedCenter/totalMass));
        report.set("mass_basis",V::makeStr("sum of exact member solids after authored joints; untrimmed overlaps count per member"));
    }else report.set("mass_reason",V::makeStr("One or more profiles has no positive density"));
    result.set("frame_report",std::move(report));return result;
}

} // namespace ark::occt
