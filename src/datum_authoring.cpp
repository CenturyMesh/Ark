#include "ark/datum_authoring.hpp"
#include "ark/sketch.hpp"
#include "ark/serialize.hpp"
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
void require(bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);}
void boundedText(const V& value,const char* what){require(value.t==V::Str&&!value.str.empty()&&value.str.size()<=128,std::string("Invalid datum ")+what);}
void expression(const V& value){require((value.t==V::Num&&std::isfinite(value.num)&&std::abs(value.num)<=1e7)||(value.t==V::Str&&!value.str.empty()&&value.str.size()<=2048),"Datum scalar must be a finite number or expression");}
void keys(const V& value,std::initializer_list<const char*> allowed,std::initializer_list<const char*> required={}){
    require(value.t==V::Obj,"Datum reference/support must be an object");
    for(const auto& kv:value.obj){bool known=false;for(auto key:allowed)known|=kv.first==key;require(known,"Unknown datum support field: "+kv.first);}
    for(auto key:required)require(value.find(key)!=nullptr,std::string("Missing datum support field: ")+key);
}
void anchor(const V& value,const char* use){
    const bool point=std::string(use)=="point",plane=std::string(use)=="plane";
    if(value.t==V::Str){boundedText(value,"reference name");return;}
    if(value.t==V::Arr){require(point&&value.arr.size()==3,"Explicit point needs exactly three coordinates");for(const auto& item:value.arr)expression(item);return;}
    require(value.t==V::Obj,"Datum anchor must be a name or typed reference");
    if(value.find("datum")){
        keys(value,{"datum","axis","local"},{"datum"});boundedText(*value.find("datum"),"reference name");
        if(auto local=value.find("local")){require(point,"Local coordinates require a point anchor");require(local->t==V::Arr&&local->arr.size()==3,"Datum local point needs three coordinates");for(const auto& p:local->arr)expression(p);}
        if(auto axis=value.find("axis")){require(!point&&!plane&&axis->t==V::Str&&(axis->str=="x"||axis->str=="y"||axis->str=="z"),"Explicit datum axis must be x, y or z");}return;
    }
    if(value.find("feature")){
        keys(value,{"feature","reference","fraction","endpoint","local"},{"feature","reference"});const auto& f=*value.find("feature");
        require(f.t==V::Num&&f.num>=1&&f.num<=9007199254740990.&&f.num==std::floor(f.num),"Datum producer must be a stable numeric feature ID");
        boundedText(*value.find("reference"),"semantic reference");
        require(!(value.find("fraction")&&value.find("endpoint")),"Datum endpoint and fraction are mutually exclusive");
        if(auto local=value.find("local")){require(point&&!value.find("endpoint"),"Local point offsets cannot replace curve endpoints");require(local->t==V::Arr&&local->arr.size()==3,"Datum local point needs three coordinates");for(const auto& p:local->arr)expression(p);}
        if(auto p=value.find("fraction"))expression(*p);
        if(auto p=value.find("endpoint"))require(point&&p->t==V::Str&&(p->str=="start"||p->str=="end"),"Curve endpoint must be start or end");return;
    }
    require(point,"Sketch endpoint is a point anchor");keys(value,{"sketch","entity","endpoint"},{"sketch","entity","endpoint"});
    boundedText(*value.find("sketch"),"sketch name");boundedText(*value.find("entity"),"entity name");
    auto p=value.find("endpoint");require(p->t==V::Str&&(p->str=="start"||p->str=="end"),"Sketch endpoint must be start or end");
}
void healthy(Document& candidate){
    candidate.recompute();require(candidate.parameterError.empty(),candidate.parameterError);
    for(const auto& sketch:candidate.sketches)require(sketch.error.empty(),"Datum publication invalidates sketch "+sketch.name+": "+sketch.error);
    for(const auto& result:candidate.lastResults)require(result.ok,"Datum publication invalidates a feature: "+result.error);
    for(const auto& d:candidate.planes)resolvePlane(candidate,d.name);
}
void guard(u64 expected,u64 current){require(expected==current,"Revision conflict; inspect the current document before datum authoring");}
}
void validateDatumDefinition(const DatumPlane& d){
    // Origin was a legal authored name before the world-zero fallback existed.
    // Definition validation is also used while loading retained source; only
    // context-aware authoring may decide whether this is a new creation.
    require(!d.name.empty()&&d.name.size()<=128&&d.name!="XY"&&d.name!="XZ"&&d.name!="YZ","Built-in datums are immutable; custom names must be 1..128 characters");
    require(d.kind=="plane"||d.kind=="axis"||d.kind=="frame"||d.kind=="point","Datum kind must be plane, axis, frame or point");
    require(!d.parent.empty()&&d.parent.size()<=128,"Datum parent must be a name");
    for(const auto& kv:d.p){require(kv.first=="ox"||kv.first=="oy"||kv.first=="oz"||kv.first=="rx"||kv.first=="ry"||kv.first=="rz","Unknown datum parameter: "+kv.first);expression(V::makeStr(kv.second));}
    if(d.support.isNull())return;
    require(d.support.t==V::Obj,"Datum support must be an object or null");const V& s=d.support;auto k=s.find("kind");require(k&&k->t==V::Str,"Datum support needs a kind");const auto& kind=k->str;
    auto ref=[&](const char* key,const char* use){auto p=s.find(key);require(p!=nullptr,std::string("Missing datum ")+key);anchor(*p,use);};
    auto scalar=[&](const char* key){auto p=s.find(key);require(p!=nullptr,std::string("Missing datum ")+key);expression(*p);};
    std::string output;
    if(kind=="offset_plane"){keys(s,{"kind","plane","offset"},{"plane","offset"});ref("plane","plane");scalar("offset");output="plane";}
    else if(kind=="angle_plane"){keys(s,{"kind","plane","axis","angle"},{"plane","axis","angle"});ref("plane","plane");ref("axis","axis");scalar("angle");output="plane";}
    else if(kind=="plane_three_points"){keys(s,{"kind","a","b","c"},{"a","b","c"});for(auto key:{"a","b","c"})ref(key,"point");output="plane";}
    else if(kind=="plane_axis_point"||kind=="plane_point_normal"){keys(s,{"kind","axis","point"},{"axis","point"});ref("axis","axis");ref("point","point");output="plane";}
    else if(kind=="axis_points"||kind=="between_points"){
        keys(s,{"kind","start","end","fraction","roll","x_axis"},{"start","end"});ref("start","point");ref("end","point");
        for(auto key:{"fraction","roll"})if(s.find(key))scalar(key);if(s.find("x_axis"))ref("x_axis","axis");output=kind=="axis_points"?"axis":"frame";
    }
    else if(kind=="axis_planes"){keys(s,{"kind","first","second"},{"first","second"});ref("first","plane");ref("second","plane");output="axis";}
    else if(kind=="bisector"){keys(s,{"kind","first","second","side"},{"first","second"});ref("first","plane");ref("second","plane");if(auto side=s.find("side"))require(side->t==V::Str&&(side->str=="sum"||side->str=="difference"),"Bisector side must be sum or difference");output="plane";}
    else if(kind=="point"){keys(s,{"kind","point"},{"point"});ref("point","point");output="point";}
    else {
        if(kind=="tube_path")keys(s,{"kind","feature","station"},{"feature","station"});
        else if(kind=="midplane")keys(s,{"kind","feature","reference","fraction","second_feature","second_reference"},{"feature","reference","second_reference"});
        else if(kind=="on_face")keys(s,{"kind","feature","reference","fraction","u","v"},{"feature","reference"});
        else if(kind=="face"||kind=="normal_to_curve"||kind=="on_curve")keys(s,{"kind","feature","reference","fraction"},{"feature","reference"});
        else throw std::runtime_error("Unsupported construction datum support kind: "+kind);
        const auto& f=*s.find("feature");require(f.t==V::Num&&f.num>=1&&f.num<=9007199254740990.&&f.num==std::floor(f.num),"Invalid datum producer");
        if(auto f2=s.find("second_feature"))require(f2->t==V::Num&&f2->num>=1&&f2->num<=9007199254740990.&&f2->num==std::floor(f2->num),"Invalid second datum producer");
        for(auto key:{"reference","second_reference"})if(auto p=s.find(key))boundedText(*p,"semantic reference");
        for(auto key:{"station","fraction","u","v"})if(s.find(key))scalar(key);
    }
    if(!output.empty())require(d.kind==output||d.kind=="frame"||(output=="frame"&&(d.kind=="plane"||d.kind=="axis")),"Datum display kind is incompatible with its construction recipe");
}
Document stageDatumSet(const Document& document,const DatumPlane& datum,u64 expected,u64 current){
    guard(expected,current);validateDatumDefinition(datum);auto candidate=document.authoringCopy();
    auto existing=std::find_if(candidate.planes.begin(),candidate.planes.end(),[&](const DatumPlane& d){return d.name==datum.name;});
    if(existing==candidate.planes.end()){require(datum.name!="Origin","Built-in datums are immutable; cannot create a new authored Origin");require(candidate.planes.size()<128,"Datum capacity exceeded");candidate.planes.push_back(datum);}else *existing=datum;
    healthy(candidate);return candidate;
}
Document stageDatumRemove(const Document& document,const std::string& name,u64 expected,u64 current){
    guard(expected,current);auto candidate=document.authoringCopy();auto existing=std::find_if(candidate.planes.begin(),candidate.planes.end(),[&](const DatumPlane& d){return d.name==name;});
    require(existing!=candidate.planes.end(),"Construction datum not found: "+name);candidate.planes.erase(existing);validateDatumTransition(document,candidate);healthy(candidate);return candidate;
}
void validateDatumTransition(const Document& before,const Document& after){
    auto retained=[](const Document& document){return std::any_of(document.planes.begin(),document.planes.end(),[](const DatumPlane& datum){return datum.name=="Origin";});};
    if(!retained(before)||retained(after))return;
    auto reject=[](){throw std::runtime_error("Cannot remove authored Origin with remaining consumers; this would silently rebind them to the built-in origin");};
    auto anchorUsesOrigin=[](const V& value){if(value.t==V::Str)return value.str=="Origin";const auto* name=value.find("datum");return name&&name->t==V::Str&&name->str=="Origin";};
    for(const auto& datum:after.planes){if(datum.support.isNull()){if(datum.parent=="Origin")reject();}else for(const char* key:{"plane","axis","point","a","b","c","start","end","first","second","x_axis"})if(const auto* anchor=datum.support.find(key);anchor&&anchorUsesOrigin(*anchor))reject();}
    for(const auto& sketch:after.sketches)if(sketch.sk.plane=="Origin")reject();
    for(const auto& canvas:after.canvases)if(canvas.plane=="Origin")reject();
    for(const auto& stamp:after.stamps)if(!stamp.feature&&stamp.plane=="Origin")reject();
    // Scan only geometric selector fields. Labels, BOM provenance, imported
    // packages and display buffers cannot manufacture a datum dependency.
    std::function<bool(const V&,const std::string&)> usesOrigin=[&](const V& value,const std::string& key){
        if(key=="name"||key=="part"||key=="component_metadata"||key=="package"||key=="mesh"||key=="appearance"||key=="source_topology")return false;
        if(value.t==V::Str)return value.str=="Origin"&&(key=="plane"||key=="datum"||key=="axis"||key=="start_ref"||key=="to_ref"||key=="through_ref");
        if(value.t==V::Arr){for(const auto& item:value.arr)if(usesOrigin(item,key))return true;}
        else if(value.t==V::Obj)for(const auto& item:value.obj)if(usesOrigin(item.second,item.first))return true;
        return false;
    };
    for(const auto& feature:after.features)if(feature.get("plane")=="Origin"||usesOrigin(feature.nativeSpec,"")||usesOrigin(feature.sourceData,""))reject();
    V serialized;if(!arkjson::parse(documentToJson(after,false,false),serialized))throw std::runtime_error("Cannot inspect datum consumers in the candidate authoring");
    // Spatial sketch constraints, projected entity recipes and skeleton source
    // carry their selectors inside persisted source rather than top-level fields.
    for(const char* section:{"sketches","skeleton","drawings"})if(const auto* value=serialized.find(section);value&&usesOrigin(*value,""))reject();
}
void bindDatumSupportProducers(const Document& document,V& support){
    if(support.t==V::Obj){
        for(auto& kv:support.obj){if((kv.first=="feature"||kv.first=="second_feature")&&kv.second.t==V::Str){
            const Feature* found=nullptr;for(const auto& f:document.features)if(f.name==kv.second.str){require(!found,"Ambiguous datum producer name");found=&f;}
            require(found!=nullptr,"Datum producer not found: "+kv.second.str);kv.second=V::makeNum((double)found->id);
        }else bindDatumSupportProducers(document,kv.second);}
    }else if(support.t==V::Arr)for(auto& v:support.arr)bindDatumSupportProducers(document,v);
}
V inspectDatum(const Document& document,const std::string& name){
    const auto frame=resolvePlane(document,name);V result=V::makeObj(),matrix=V::makeArr(),origin=V::makeArr(),directions=V::makeObj();result.set("name",V::makeStr(name));result.set("kind",V::makeStr("plane"));
    const bool authored=std::any_of(document.planes.begin(),document.planes.end(),[&](const DatumPlane& datum){return datum.name==name;});
    const bool builtin=!authored&&(name=="Origin"||name=="XY"||name=="XZ"||name=="YZ");
    result.set("builtin",V::makeBool(builtin));result.set("immutable",V::makeBool(builtin));if(authored&&name=="Origin")result.set("role",V::makeStr("legacy_authored_datum"));
    if(builtin){result.set("builtin",V::makeBool(true));result.set("immutable",V::makeBool(true));if(name=="Origin"){result.set("kind",V::makeStr("point"));result.set("role",V::makeStr("starting_point"));}else result.set("role",V::makeStr("standard_plane"));}
    for(double n:frame.m)matrix.arr.push_back(V::makeNum(n));result.set("world_column_major",matrix);
    for(double n:{frame.m[12],frame.m[13],frame.m[14]})origin.arr.push_back(V::makeNum(n));result.set("origin_mm",origin);result.set("coordinate_space",V::makeStr("world"));
    for(size_t axis=0;axis<3;++axis){V direction=V::makeArr();for(size_t i=0;i<3;++i)direction.arr.push_back(V::makeNum(frame.m[axis*4+i]));directions.set(std::string(1,"XYZ"[axis]),direction);}result.set("axes",directions);
    for(const auto& d:document.planes)if(d.name==name){V p=V::makeObj();for(const auto& kv:d.p)p.set(kv.first,V::makeStr(kv.second));result.set("kind",V::makeStr(d.kind));result.set("support",d.support);result.set("parent",V::makeStr(d.parent));result.set("p",p);}
    result.set("authority",V::makeStr("authored_expressions_and_semantic_sources"));result.set("associative",V::makeBool(true));return result;
}
}
