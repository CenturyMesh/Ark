#include "ark/occt_frame.hpp"
#include "ark/occt_solid.hpp"
#include "common.hpp"

#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
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
    double developedLength=0,authoredLength=0;V result;
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
    m.authoredLength=m.developedLength;return m;
}
TopoDS_Shape outerEnvelope(const Member& host,const Profile& profile){
    if(host.bent)throw std::runtime_error("Endpoint trimming requires a straight host envelope");
    if(profile.section=="round")return BRepPrimAPI_MakeCylinder(gp_Ax2(host.start,host.startTangent),profile.od/2,host.developedLength).Shape();
    const auto box=BRepPrimAPI_MakeBox(gp_Pnt(0,-profile.width/2,-profile.height/2),host.developedLength,profile.width,profile.height).Shape();return transformed(box,rigid(frame(host.start,host.startTangent,host.startNormal)));
}
double envelopeRadius(const Profile& profile){return profile.section=="round"?profile.od/2:std::hypot(profile.width,profile.height)/2;}
void adjustEndpoint(Member& member,const Member& host,const Profile& own,const V& joint){
    if(member.bent||host.bent||member.name==host.name)throw std::runtime_error("trim_extend requires two different straight frame members");
    const double maximum=scalar(joint,"max_extension");if(maximum<0)throw std::runtime_error("max_extension must be a declared nonnegative length");
    const gp_Vec u(member.startTangent),v(host.startTangent),w(host.start,member.start);const double b=u.Dot(v),d=u.Dot(w),e=v.Dot(w),denominator=1-b*b;
    if(denominator<1e-8)throw std::runtime_error("trim_extend rejects parallel axes; use an explicit crossing trim plane");
    const double s=(b*e-d)/denominator,t=(e-b*d)/denominator;const auto target=member.start.Translated(u*s),hostPoint=host.start.Translated(v*t);
    if(target.Distance(hostPoint)>1e-6)throw std::runtime_error("trim_extend supports intersecting member axes; offset/skew joints need explicit geometry");
    const double margin=envelopeRadius(own)/std::sqrt(denominator);if(t<=margin+1e-6||t>=host.developedLength-margin-1e-6)throw std::runtime_error("trim_extend target is too close to a host end for the declared member profile");
    const bool start=word(joint,"end")=="start";const auto original=start?member.start:member.end;const gp_Vec outward=start?-u:u;const double growth=gp_Vec(original,target).Dot(outward);if(growth>maximum+1e-7)throw std::runtime_error("Required frame extension exceeds declared max_extension");
    if(start)member.start=target;else member.end=target;const gp_Vec chord(member.start,member.end);if(chord.Dot(u)<=1e-6)throw std::runtime_error("trim_extend would remove or reverse the member");member.developedLength=chord.Magnitude();
}
void trimEnvelope(Member& member,const Member& host,const Profile& profile){
    const auto envelope=outerEnvelope(host,profile);V cut=V::makeObj();cut.set("op",V::makeStr("cut"));cut.set("input_brep",required(member.result,"brep_base64"));cut.set("input_topology",required(member.result,"topology"));cut.set("tool_brep",V::makeStr(brepData(envelope)));const double before=required(required(member.result,"report"),"volume_mm3").num;auto outcome=modelSolidNative(cut);const double after=required(required(outcome,"report"),"volume_mm3").num;if(before-after<=1e-8||after<=1e-8||required(required(outcome,"report"),"solids").num!=1)throw std::runtime_error("Endpoint trim must remove stock and leave one positive-volume member; inspect the selected end and host envelope");member.result=outcome;
}
void trimPlane(Member& member,const V& joint){
    if(member.bent)throw std::runtime_error("trim_plane currently supports a straight frame member");const auto origin=point(required(joint,"origin"),"Trim plane origin"),vector=point(required(joint,"normal"),"Trim plane normal");gp_Vec n(vector.X(),vector.Y(),vector.Z());if(n.Magnitude()<1e-9)throw std::runtime_error("Trim plane normal must be nonzero");n.Normalize();const bool start=word(joint,"end")=="start";const double selected=gp_Vec(origin,start?member.start:member.end).Dot(n),retained=gp_Vec(origin,start?member.end:member.start).Dot(n);if(selected*retained>=0||std::abs(selected)<1e-8||std::abs(retained)<1e-8)throw std::runtime_error("trim_plane must cross the member axis between the selected end and retained end");V split=V::makeObj();split.set("op",V::makeStr("split_plane"));split.set("input_brep",required(member.result,"brep_base64"));split.set("input_topology",required(member.result,"topology"));split.set("origin",required(joint,"origin"));split.set("normal",required(joint,"normal"));split.set("keep",V::makeStr(retained>0?"positive":"negative"));member.result=modelSolidNative(split);
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

V tubeEndFitNative(const V& request){
    const auto mode=word(request,"mode"),end=word(request,"end");const bool start=end=="start";
    if((mode!="envelope"&&mode!="plane"&&mode!="plane_trim")||(end!="start"&&end!="end"))throw std::runtime_error("Tube end fit needs mode envelope|plane|plane_trim and end start|end");
    const double maximum=scalar(request,"max_extension");if(maximum<0)throw std::runtime_error("Tube end fit max_extension must be nonnegative");
    const auto& authority=required(request,"tube_authority");const auto& program=required(authority,"program");const auto& segments=required(program,"segments");
    if(segments.t!=V::Arr||segments.arr.empty()||word(start?segments.arr.front():segments.arr.back(),"kind")!="line")throw std::runtime_error("Tube end fit requires a straight terminal segment at the selected end");
    auto memberFrom=[&](const V& authored){Member m;m.start=gp_Pnt(0,0,0);m.startTangent=gp_Dir(1,0,0);m.startNormal=gp_Dir(0,1,0);advance(m,required(required(authored,"program"),"segments"));const auto place=rigid(required(authored,"frame"));m.start.Transform(place);m.end.Transform(place);for(auto* d:{&m.startTangent,&m.endTangent,&m.startNormal,&m.endNormal})*d=gp_Dir(gp_Vec(*d).Transformed(place));return m;};
    Member source=memberFrom(authority);const auto tangent=start?source.startTangent:source.endTangent;const gp_Vec outward=start?-gp_Vec(tangent):gp_Vec(tangent);const auto selected=start?source.start:source.end;
    const double terminal=positive(start?segments.arr.front():segments.arr.back(),"length");const auto inner=selected.Translated(-outward*terminal);
    const auto original=readBrep(required(request,"input_brep").str),host=readBrep(required(request,"tool_brep").str);shapeReport(original,true);shapeReport(host,false);
    auto halfSpace=[&](const gp_Pnt& origin,const gp_Dir& normal,const gp_Pnt& inside){BRepBuilderAPI_MakeFace f{gp_Pln(origin,normal)};return BRepPrimAPI_MakeHalfSpace(f.Face(),inside).Solid();};
    auto exactCommon=[&](const TopoDS_Shape& a,const TopoDS_Shape& b){BRepAlgoAPI_Common operation(a,b);operation.Build();if(!operation.IsDone()||operation.HasErrors())throw std::runtime_error("Tube end fit exact intersection failed");return operation.Shape();};
    const double before=properties(original);V result,evidence=V::makeObj();evidence.set("mode",V::makeStr(mode));evidence.set("end",V::makeStr(end));evidence.set("source_tube",required(authority,"origin_id"));evidence.set("exact",V::makeBool(true));evidence.set("max_extension_mm",V::makeNum(maximum));
    double generatedGrowth=0,axisChange=0;TopoDS_Shape originalCap;
    const auto& inputReferences=required(required(request,"input_topology"),"references");const V* endpointReference=nullptr;for(const auto& ref:inputReferences.arr)if(word(ref,"name")==std::string("cap.")+end)endpointReference=&ref;
    if(!endpointReference||(word(*endpointReference,"status")!="bound"&&word(*endpointReference,"status")!="merged"))throw std::runtime_error("Tube end fit selected original cap is unavailable; inspect earlier endpoint operations");const auto& endpointIndices=required(*endpointReference,"indices");const auto sourceFaces=shapes(original,TopAbs_FACE);if(endpointIndices.arr.size()!=1||endpointIndices.arr[0].num<0||endpointIndices.arr[0].num>=sourceFaces.size())throw std::runtime_error("Tube end fit endpoint cap binding is invalid");originalCap=sourceFaces[(size_t)endpointIndices.arr[0].num];
    if(mode=="envelope"){
        if(request.find("reference"))throw std::runtime_error("Envelope fit does not take a face reference");if(maximum!=0)throw std::runtime_error("Envelope tube end fit currently trims existing stock; max_extension must be 0");
        const auto& hostAuthority=required(request,"host_authority");const auto& stock=required(hostAuthority,"program");const auto& hostSegments=required(stock,"segments");if(hostSegments.arr.size()!=1||word(hostSegments.arr[0],"kind")!="line")throw std::runtime_error("Envelope fit currently requires a straight authored Tube host");
        auto support=memberFrom(hostAuthority);support.startTangent=support.endTangent;support.startNormal=support.endNormal;support.developedLength=positive(hostSegments.arr[0],"length");Profile profile;profile.section=word(stock,"op")=="rect_tube"?"rect":"round";if(profile.section=="rect"){profile.width=positive(stock,"width");profile.height=positive(stock,"height");}else profile.od=positive(stock,"od");
        const auto envelope=outerEnvelope(support,profile);const auto removed=exactCommon(original,envelope);const double removedVolume=properties(removed);if(removedVolume<=1e-8)throw std::runtime_error("Tube end fit host envelope removes no stock");
        BRepExtrema_DistShapeShape capDistance(originalCap,envelope);if(!capDistance.IsDone()||capDistance.Value()>1e-6)throw std::runtime_error("Tube end fit host does not reach the selected endpoint cap");
        const auto beyond=halfSpace(inner,gp_Dir(outward),inner.Translated(-outward));if(std::abs(properties(exactCommon(removed,beyond)))>1e-8)throw std::runtime_error("Tube end fit would cut outside the selected terminal straight segment");
        V cut=V::makeObj();cut.set("op",V::makeStr("cut"));cut.set("input_brep",required(request,"input_brep"));cut.set("input_topology",required(request,"input_topology"));cut.set("tool_brep",V::makeStr(brepData(envelope)));result=modelSolidNative(cut);evidence.set("host_tube",required(hostAuthority,"origin_id"));evidence.set("tool_basis",V::makeStr("exact filled outer stock of authored straight host; host physical member retained"));
    }else{
        const auto& topology=required(request,"tool_topology");const auto reference=word(request,"reference");const auto& refs=required(topology,"references");const V* chosen=nullptr;for(const auto& ref:refs.arr)if(word(ref,"name")==reference){if(chosen)throw std::runtime_error("Ambiguous tube end fit face");chosen=&ref;}
        if(!chosen||word(*chosen,"kind")!="face"||(word(*chosen,"status")!="bound"&&word(*chosen,"status")!="merged"))throw std::runtime_error("Tube end fit needs a bound host face");
        const auto& indices=required(*chosen,"indices");if(indices.arr.size()!=1)throw std::runtime_error("Tube end fit host face is ambiguous");const auto hostFaces=shapes(host,TopAbs_FACE);const int hostIndex=(int)indices.arr[0].num;if(hostIndex<0||(size_t)hostIndex>=hostFaces.size())throw std::runtime_error("Tube end fit host face binding is stale");
        BRepAdaptor_Surface hostSurface(TopoDS::Face(hostFaces[hostIndex]));if(hostSurface.GetType()!=GeomAbs_Plane)throw std::runtime_error("Tube end fit requires an actual planar host face");
        const auto& descriptor=required(*chosen,"descriptor");const auto origin=point(required(descriptor,"origin"),"Host plane origin"),normalPoint=point(required(descriptor,"normal"),"Host plane normal");const gp_Dir normal(normalPoint.X(),normalPoint.Y(),normalPoint.Z());const gp_Vec n(normal);const double divisor=outward.Dot(n);if(std::abs(divisor)<1e-8)throw std::runtime_error("Tube end fit terminal axis is parallel to the host plane");
        const auto sectionNormal=start?source.startNormal:source.endNormal;const gp_Dir binormal=tangent.Crossed(sectionNormal);const bool rect=word(program,"op")=="rect_tube";const double spread=(rect?(positive(program,"width")*std::abs(gp_Vec(sectionNormal).Dot(n))+positive(program,"height")*std::abs(gp_Vec(binormal).Dot(n)))/2:positive(program,"od")/2*std::sqrt(std::max(0.,1-divisor*divisor)))/std::abs(divisor);
        axisChange=gp_Vec(selected,origin).Dot(n)/divisor;const double requiredGrowth=std::max(0.,axisChange+spread);if(requiredGrowth>maximum+1e-7)throw std::runtime_error("Tube end fit extension exceeds declared max_extension");if(axisChange-spread<=-terminal+1e-7)throw std::runtime_error("Tube end fit plane crosses outside the selected terminal straight segment");
        V stock=V::makeObj();stock.set("brep_base64",required(request,"input_brep"));stock.set("topology",required(request,"input_topology"));
        if(requiredGrowth>1e-8){
            V base=program;auto baseResult=modelSolidNative(base);V move=V::makeObj();move.set("op",V::makeStr("transform"));move.set("input_brep",required(baseResult,"brep_base64"));move.set("input_topology",required(baseResult,"topology"));move.set("frame",required(authority,"frame"));auto regenerated=modelSolidNative(move);if(symmetricDifference(original,readBrep(required(regenerated,"brep_base64").str))>std::max(1e-8,before*1e-10))throw std::runtime_error("Extension requires original untrimmed Tube geometry; retained earlier cuts cannot be regenerated");
            generatedGrowth=maximum;V changed=segments;auto& terminalSegment=start?changed.arr.front():changed.arr.back();terminalSegment.set("length",V::makeNum(terminal+maximum));base.set("segments",changed);baseResult=modelSolidNative(base);move.set("input_brep",required(baseResult,"brep_base64"));move.set("input_topology",required(baseResult,"topology"));auto placement=required(authority,"frame");if(start){placement.arr[12].num-=source.startTangent.X()*maximum;placement.arr[13].num-=source.startTangent.Y()*maximum;placement.arr[14].num-=source.startTangent.Z()*maximum;}move.set("frame",placement);stock=modelSolidNative(move);
        }
        const double retained=gp_Vec(origin,inner).Dot(n);V split=V::makeObj();split.set("op",V::makeStr("split_plane"));split.set("input_brep",required(stock,"brep_base64"));split.set("input_topology",required(stock,"topology"));split.set("origin",xyz(origin));split.set("normal",direction(normal));split.set("keep",V::makeStr(retained>0?"positive":"negative"));
        if(std::abs(axisChange-generatedGrowth)<1e-8&&spread<1e-8){if(generatedGrowth==0)throw std::runtime_error("Tube end fit endpoint is already aligned with the selected plane");result=stock;}else result=modelSolidNative(split);
        const auto outcome=readBrep(required(result,"brep_base64").str);TopoDS_Shape cap;int capIndex=-1,index=0;for(const auto& candidate:shapes(outcome,TopAbs_FACE)){BRepAdaptor_Surface surface(TopoDS::Face(candidate));if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().Dot(normal))>1-1e-9&&std::abs(gp_Vec(origin,surface.Plane().Location()).Dot(n))<1e-7){if(!cap.IsNull())throw std::runtime_error("Tube end fit has ambiguous planar end faces");cap=candidate;capIndex=index;}++index;}
        if(cap.IsNull())throw std::runtime_error("Tube end fit has no exact cap at the selected host plane");GProp_GProps full,covered;BRepGProp::SurfaceProperties(cap,full);BRepGProp::SurfaceProperties(exactCommon(cap,hostFaces[hostIndex]),covered);const double uncovered=std::max(0.,full.Mass()-covered.Mass());const bool fullSeat=uncovered<=std::max(1e-7,full.Mass()*1e-9);if(mode=="plane"&&!fullSeat)throw std::runtime_error("Tube end fit cap extends beyond the selected finite host face; choose explicit plane_trim for partial seating or exact envelope coping");
        auto registry=required(result,"topology"),rows=required(registry,"references");V ref=V::makeObj(),binding=V::makeArr();binding.arr.push_back(V::makeNum(capIndex));ref.set("name",V::makeStr("fit."+end));ref.set("kind",V::makeStr("face"));ref.set("status",V::makeStr("bound"));ref.set("indices",binding);ref.set("frame_available",V::makeBool(true));V datum=V::makeObj();datum.set("origin",xyz(origin));datum.set("normal",direction(normal));datum.set("x_axis",direction(BRepAdaptor_Surface(TopoDS::Face(cap)).Plane().XAxis().Direction()));ref.set("descriptor",datum);rows.arr.push_back(ref);registry.set("references",rows);result.set("topology",registry);evidence.set("reference",V::makeStr(reference));evidence.set("cap_area_mm2",V::makeNum(full.Mass()));evidence.set("cap_coverage_checked",V::makeBool(true));evidence.set("uncovered_cap_area_mm2",V::makeNum(uncovered));evidence.set("covered_cap_area_mm2",V::makeNum(covered.Mass()));evidence.set("full_seat_checked",V::makeBool(fullSeat));evidence.set("support_scope",V::makeStr("exact supporting-plane trim; measured finite-face coverage does not qualify sealing, weld continuity or fabrication"));
    }
    const auto fitted=readBrep(required(result,"brep_base64").str);shapeReport(fitted,true);BRepExtrema_DistShapeShape distance(fitted,host);if(!distance.IsDone()||distance.Value()>1e-6)throw std::runtime_error("Tube end fit result does not touch the physical host");const double overlap=std::abs(properties(exactCommon(fitted,host)));if(overlap>1e-8)throw std::runtime_error("Tube end fit leaves material overlap with the physical host");
    if(!originalCap.IsNull()){GProp_GProps full,remaining;BRepGProp::SurfaceProperties(originalCap,full);BRepGProp::SurfaceProperties(exactCommon(fitted,originalCap),remaining);evidence.set("original_cap_area_mm2",V::makeNum(full.Mass()));evidence.set("remaining_original_cap_area_mm2",V::makeNum(remaining.Mass()));evidence.set("original_cap_fully_removed",V::makeBool(remaining.Mass()<=1e-8));evidence.set("full_terminal_coverage_checked",V::makeBool(false));evidence.set("coverage_scope",V::makeStr("exact retained area on the original selected cap; zero overlap and contact do not prove complete seating, sealing or weld continuity"));}
    evidence.set("source_volume_mm3",V::makeNum(before));evidence.set("result_volume_mm3",required(required(result,"report"),"volume_mm3"));evidence.set("volume_change_mm3",V::makeNum(properties(fitted)-before));evidence.set("generated_extension_mm",V::makeNum(generatedGrowth));evidence.set("axis_length_change_mm",V::makeNum(axisChange));evidence.set("separation_mm",V::makeNum(distance.Value()));evidence.set("overlap_volume_mm3",V::makeNum(overlap));evidence.set("terminal_ownership_checked",V::makeBool(true));evidence.set("weld_strength_checked",V::makeBool(false));auto report=required(result,"report");report.set("end_fit",evidence);result.set("report",report);return result;
}

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
        members.push_back(std::move(member));
    }
    if(const V* authored=request.find("joints")){
        if(authored->t!=V::Arr||authored->arr.size()>64)throw std::runtime_error("Frame joints need 0..64 entries");std::set<std::pair<std::string,std::string>> applied;
        for(const auto& joint:authored->arr){const auto kind=word(joint,"kind"),name=word(joint,"member"),end=word(joint,"end");if(kind!="cope"&&kind!="trim_extend"&&kind!="trim_plane")throw std::runtime_error("Frame joint kind must be cope, trim_extend or trim_plane");if(end!="start"&&end!="end")throw std::runtime_error("Frame joint end must be start or end");if(!applied.insert({name,end}).second)throw std::runtime_error("Frame member end has more than one joint");auto target=memberIndex.find(name);if(target==memberIndex.end())throw std::runtime_error("Frame joint names an unknown member");if(kind=="trim_extend"){auto host=memberIndex.find(word(joint,"against"));if(host==memberIndex.end())throw std::runtime_error("Frame trim host not found");adjustEndpoint(members[target->second],members[host->second],profiles.at(members[target->second].profile),joint);}}
    }
    for(size_t index=0;index<members.size();++index)members[index].result=makeTube(members[index],profiles.at(members[index].profile),memberRows.arr[index]);
    V joints=V::makeArr();
    if(const V* authored=request.find("joints")){
        if(authored->t!=V::Arr||authored->arr.size()>64)throw std::runtime_error("Frame joints need 0..64 entries");
        std::set<std::pair<std::string,std::string>> applied;
        for(const V& joint:authored->arr){
            const std::string kind=word(joint,"kind"),name=word(joint,"member"),end=word(joint,"end");
            if(end!="start"&&end!="end")throw std::runtime_error("Frame cope end must be start or end");
            if(!applied.insert({name,end}).second)throw std::runtime_error("Frame member end has two authored copes");
            const auto member=memberIndex.find(name);if(member==memberIndex.end())throw std::runtime_error("Frame joint names an unknown member");
            Member& target=members[member->second];
            const double before=required(required(target.result,"report"),"volume_mm3").num;
            std::string against;if(kind=="trim_plane")trimPlane(target,joint);else{against=word(joint,"against");const auto host=memberIndex.find(against);if(host==memberIndex.end())throw std::runtime_error("Frame joint host not found");const auto& support=members[host->second];if(kind=="cope")cope(target,support,profiles.at(target.profile),profiles.at(support.profile),end);else trimEnvelope(target,support,profiles.at(support.profile));}
            const double after=required(required(target.result,"report"),"volume_mm3").num;
            V evidence=V::makeObj();evidence.set("kind",V::makeStr(kind));
            evidence.set("member",V::makeStr(name));evidence.set("end",V::makeStr(end));
            if(!against.empty())evidence.set("against",V::makeStr(against));evidence.set("removed_volume_mm3",V::makeNum(before-after));evidence.set("axis_length_change_mm",V::makeNum(target.developedLength-target.authoredLength));
            evidence.set("exact",V::makeBool(true));joints.arr.push_back(std::move(evidence));
        }
    }
    std::vector<TopoDS_Shape> shapesForCompound;shapesForCompound.reserve(members.size());
    V cutlist=V::makeArr(),stockRows=V::makeArr();double totalLength=0,totalMass=0,stockY=0;bool massChecked=true;
    gp_XYZ weightedCenter(0,0,0);
    GProp_GProps physicalProperties;
    for(const Member& member:members){
        const Profile& profile=profiles.at(member.profile);
        const TopoDS_Shape solid=readBrep(required(member.result,"brep_base64").str);
        shapesForCompound.push_back(solid);
        const double volume=required(required(member.result,"report"),"volume_mm3").num;
        if(!(volume>0))throw std::runtime_error("Frame member has no positive exact volume");
        GProp_GProps exactProperties;volumeCentroidProperties(solid,exactProperties);
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
        row.set("authored_length_mm",V::makeNum(member.authoredLength));row.set("length_basis",V::makeStr("generated tube centerline stock before exact end cuts; endpoint frames are design datums"));
        row.set("section_area_mm2",V::makeNum(profile.area));
        double iy=0,iz=0,cy=0,cz=0;if(profile.section=="round"){const double inner=profile.od-2*profile.wall;iy=iz=std::acos(-1.0)*(std::pow(profile.od,4)-std::pow(inner,4))/64;cy=cz=profile.od/2;}else{const double wi=profile.width-2*profile.wall,hi=profile.height-2*profile.wall;iy=(profile.width*std::pow(profile.height,3)-wi*std::pow(hi,3))/12;iz=(profile.height*std::pow(profile.width,3)-hi*std::pow(wi,3))/12;cy=profile.height/2;cz=profile.width/2;}row.set("second_moment_y_mm4",V::makeNum(iy));row.set("second_moment_z_mm4",V::makeNum(iz));row.set("min_second_moment_mm4",V::makeNum(std::min(iy,iz)));row.set("extreme_fiber_y_mm",V::makeNum(cy));row.set("extreme_fiber_z_mm",V::makeNum(cz));row.set("section_property_basis",V::makeStr("ideal nominal hollow profile; local y=member normal/width,z=tangent cross normal/height; not mass inertia or end-cut section"));
        row.set("nominal_blank_volume_mm3",V::makeNum(profile.area*member.developedLength));
        row.set("exact_volume_mm3",V::makeNum(volume));row.set("center_mm",xyz(center));
        row.set("start",endpoint(member.start,member.startTangent,member.startNormal));
        row.set("end",endpoint(member.end,member.endTangent,member.endNormal));
        if(!profile.material.empty())row.set("material",V::makeStr(profile.material));
        if(profile.density>0){const double mass=profile.density*volume/1000;
            physicalProperties.Add(exactProperties,profile.density/1000.);
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
    V weldRows=V::makeArr();std::vector<TopoDS_Shape> beadShapes;
    if(const auto* welds=request.find("welds")){
        if(welds->t!=V::Arr||welds->arr.size()>64)throw std::runtime_error("Frame welds need 0..64 entries");std::set<std::string> names;
        for(const auto& weld:welds->arr){const auto id=word(weld,"id"),a=word(weld,"member_a"),b=word(weld,"member_b");if(!names.insert(id).second||a==b||!memberIndex.count(a)||!memberIndex.count(b))throw std::runtime_error("Weld requires a unique id and two distinct existing frame members");const auto& path=required(weld,"path");if(path.t!=V::Arr||path.arr.size()!=2)throw std::runtime_error("Weld supports one straight seam with two endpoints");const auto start=point(path.arr[0],"Weld start"),end=point(path.arr[1],"Weld end"),normal=point(required(weld,"normal"),"Weld normal");const gp_Vec travel(start,end);const double length=travel.Magnitude();if(length<1e-6)throw std::runtime_error("Weld seam must have positive length");const gp_Dir axis(travel);gp_Vec lateral(normal.X(),normal.Y(),normal.Z());lateral-=gp_Vec(axis)*lateral.Dot(gp_Vec(axis));if(lateral.Magnitude()<1e-8)throw std::runtime_error("Weld normal must be nonzero and perpendicular to its seam");lateral.Normalize();const gp_Vec other=gp_Vec(axis).Crossed(lateral);const double legA=positive(weld,"leg_a"),legB=positive(weld,"leg_b");BRepBuilderAPI_MakePolygon triangle;triangle.Add(start);triangle.Add(start.Translated(lateral*legA));triangle.Add(start.Translated(other*legB));triangle.Close();const auto solid=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(triangle.Wire()).Face(),travel).Shape();const auto checked=shapeReport(solid,true);const double volume=legA*legB*length/2;if(std::abs(properties(solid)-volume)>std::max(1e-6,volume*1e-8))throw std::runtime_error("Weld bead exact volume does not match its declared triangular section");
            for(const auto& member:{a,b}){const auto part=readBrep(required(members[memberIndex.at(member)].result,"brep_base64").str);BRepExtrema_DistShapeShape distance(solid,part);if(!distance.IsDone()||distance.Value()>1e-6)throw std::runtime_error("Declared weld bead does not touch both selected frame members");if(std::abs(properties(common(solid,part)))>std::max(1e-6,volume*1e-8))throw std::runtime_error("Declared weld bead overlaps member material; penetrating beads are outside this fillet geometry domain");}
            V row=V::makeObj();row.set("id",V::makeStr(id));row.set("kind",V::makeStr("declared_straight_triangular_fillet"));row.set("member_a",V::makeStr(a));row.set("member_b",V::makeStr(b));row.set("path",path);row.set("length_mm",V::makeNum(length));row.set("leg_a_mm",V::makeNum(legA));row.set("leg_b_mm",V::makeNum(legB));const double throat=legA*legB/std::hypot(legA,legB);row.set("theoretical_throat_mm",V::makeNum(throat));row.set("nominal_throat_area_mm2",V::makeNum(throat*length));row.set("exact_volume_mm3",V::makeNum(volume));row.set("attachment_checked",V::makeBool(true));row.set("attachment_scope",V::makeStr("exact zero distance to both member solids; continuous interface, fusion and penetration not verified"));row.set("weld_strength_checked",V::makeBool(false));row.set("exact",V::makeBool(true));if(const auto* material=weld.find("material"))row.set("material",*material);
            if(weld.find("density_g_cm3")&&scalar(weld,"density_g_cm3")>0&&scalar(weld,"density_g_cm3")<=100){const double density=scalar(weld,"density_g_cm3"),mass=volume*density/1000;GProp_GProps bead;volumeProperties(solid,bead);physicalProperties.Add(bead,density/1000);totalMass+=mass;weightedCenter+=bead.CentreOfMass().XYZ()*mass;row.set("mass_g",V::makeNum(mass));row.set("density_g_cm3",V::makeNum(density));}else{if(weld.find("density_g_cm3")&&(scalar(weld,"density_g_cm3")<0||scalar(weld,"density_g_cm3")>100))throw std::runtime_error("Weld density must be 0..100 g/cm3");massChecked=false;}
            for(const auto& bead:beadShapes)if(std::abs(properties(common(solid,bead)))>std::max(1e-6,volume*1e-8))throw std::runtime_error("Declared weld beads overlap material; separate bead solids must not double-count filler");
            beadShapes.push_back(solid);weldRows.arr.push_back(row);shapesForCompound.push_back(solid);
        }
    }
    TopoDS_Shape combined=compound(shapesForCompound);
    V result=shapeResult(combined,false);
    result.set("topology",topologyFor(combined,required(result,"brep_base64").str,members,shapesForCompound));
    V report=V::makeObj(),nodeReport=V::makeArr();
    for(const V& node:nodeRows.arr){V item=V::makeObj();item.set("name",required(node,"name"));item.set("point_mm",required(node,"xyz"));nodeReport.arr.push_back(std::move(item));}
    report.set("nodes",std::move(nodeReport));report.set("members",std::move(cutlist));report.set("joints",std::move(joints));
    report.set("welds",weldRows);report.set("weld_count",V::makeNum(weldRows.arr.size()));report.set("weld_strength_checked",V::makeBool(false));
    V stock=V::makeObj();stock.set("display_only",V::makeBool(true));
    stock.set("bend_trajectory_checked",V::makeBool(false));stock.set("members",std::move(stockRows));
    report.set("stock_preview",std::move(stock));
    report.set("member_count",V::makeNum(members.size()));report.set("total_developed_length_mm",V::makeNum(totalLength));
    report.set("mass_checked",V::makeBool(massChecked));
    if(massChecked){
        report.set("mass_g",V::makeNum(totalMass));
        V inertia=V::makeArr();auto tensor=physicalProperties.MatrixOfInertia();for(int i=1;i<=3;++i)for(int j=1;j<=3;++j)inertia.arr.push_back(V::makeNum(tensor.Value(i,j)));report.set("inertia_g_mm2",inertia);
        report.set("center_mm",vec(weightedCenter/totalMass));
        report.set("mass_basis",V::makeStr("sum of exact member solids after authored joints plus declared exact beads; untrimmed overlaps count per member"));
    }else report.set("mass_reason",V::makeStr("One or more member profiles or weld beads has no positive density"));
    result.set("frame_report",std::move(report));return result;
}

} // namespace ark::occt
