#include "ark/design_containers.hpp"
#include "ark/cad_import.hpp"
// ARK-Point :: serialize.cpp — document persistence over arkjson.
#include "ark/serialize.hpp"
#include "ark/modeling_031b.hpp"
#include "ark/bom.hpp"
#include "ark/agent_activity.hpp"
#include "ark/iteration_history.hpp"
#include "ark/document_container.hpp"
#include "ark/manufacturing.hpp"
#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/skeleton.hpp"
#include "ark/drawings.hpp"
#include "ark/assembly_authoring.hpp"
#include "ark/sketch.hpp"
#include "ark/assembly_io.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/usage.hpp"
#include "ark/electronics.hpp"
#include "ark/curve_pattern_044b.hpp"
#include "ark/media_044b.hpp"
#include "ark/motion_graph.hpp"
#include <cstdio>
#include <cctype>
#include <fstream>
#include <set>
#include <algorithm>
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ark {

using arkjson::Value;

static void canonicalDocumentKeys(Value& value){
    if(value.t==Value::Obj){for(auto& item:value.obj)canonicalDocumentKeys(item.second);std::sort(value.obj.begin(),value.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}
    else if(value.t==Value::Arr)for(auto& item:value.arr)canonicalDocumentKeys(item);
}

namespace {

bool indexedSourceExpression(const std::string& text){
    for(size_t i=0;i<text.size();++i)if(text[i]=='['){
        size_t previous=i;while(previous&&std::isspace((unsigned char)text[previous-1]))--previous;
        if(!previous||(!std::isalnum((unsigned char)text[previous-1])&&text[previous-1]!='_'))continue;
        int depth=1;size_t end=i+1;for(;end<text.size()&&depth;++end){if(text[end]=='[')++depth;else if(text[end]==']')--depth;}if(depth)continue;
        while(end<text.size()&&std::isspace((unsigned char)text[end]))++end;if(end==text.size()||text[end++]!='.')continue;
        while(end<text.size()&&std::isspace((unsigned char)text[end]))++end;
        if(end<text.size()&&(text[end]=='x'||text[end]=='y'||text[end]=='z')&&(end+1==text.size()||(!std::isalnum((unsigned char)text[end+1])&&text[end+1]!='_')))return true;
    }
    return false;
}
bool indexedSourceValue(const Value& value){
    if(value.t==Value::Str)return indexedSourceExpression(value.str);
    if(value.t==Value::Obj)for(const auto& item:value.obj)if(indexedSourceValue(item.second))return true;
    if(value.t==Value::Arr)for(const auto& item:value.arr)if(indexedSourceValue(item))return true;
    return false;
}
bool currentBrepReference(const std::string& text){return text.find("@ark.face.")!=std::string::npos||text.find("@ark.edge.")!=std::string::npos;}
bool currentBrepSourceValue(const Value& value){
    if(value.t==Value::Str)return currentBrepReference(value.str);
    if(value.t==Value::Obj)for(const auto& item:value.obj)if(currentBrepSourceValue(item.second))return true;
    if(value.t==Value::Arr)for(const auto& item:value.arr)if(currentBrepSourceValue(item))return true;
    return false;
}
bool generation35Document(const Document& doc){
    if(skeletonDocument(doc))return true;
    for(const auto& feature:doc.features){
        if(feature.type==FeatureType::MeshEdit)return true;
        if(feature.type==FeatureType::Curve)if(const auto* op=feature.nativeSpec.find("op");op&&op->asStr()=="helix")return true;
        if(feature.type==FeatureType::Loft&&feature.nativeSpec.find("controls"))return true;
        if(feature.type==FeatureType::Sew&&feature.nativeSpec.find("require_connected"))return true;
        if(indexedSourceValue(feature.nativeSpec)||currentBrepSourceValue(feature.nativeSpec))return true;
        for(const auto& bend:feature.bends)for(const auto* expression:{&bend.feed,&bend.radius,&bend.angle,&bend.roll})if(indexedSourceExpression(*expression))return true;
        for(const auto& notch:feature.notches)if(indexedSourceExpression(notch.station)||indexedSourceExpression(notch.angle))return true;
    }
    auto arrayExpression=indexedSourceExpression;
    for(const auto& p:doc.params)if(arrayExpression(p.expr))return true;
    for(const auto& f:doc.features)for(const auto& p:f.p)if(arrayExpression(p.second)||currentBrepReference(p.second))return true;
    for(const auto& sk:doc.sketches){for(const auto& e:sk.sk.ents){for(const auto& expr:e.expressions)if(arrayExpression(expr))return true;if(currentBrepReference(e.reference)||indexedSourceValue(e.entityRecipe)||indexedSourceValue(e.arcRecipe)||currentBrepSourceValue(e.entityRecipe)||currentBrepSourceValue(e.arcRecipe))return true;}for(const auto& con:sk.sk.cons)if(arrayExpression(con.expression)||arrayExpression(con.expression2))return true;for(const auto& corner:sk.sk.corners)if(arrayExpression(corner.size))return true;}
    for(const auto& plane:doc.planes){for(const auto& p:plane.p)if(arrayExpression(p.second))return true;if(indexedSourceValue(plane.support)||currentBrepSourceValue(plane.support))return true;}
    for(const auto& canvas:doc.canvases)if(arrayExpression(canvas.width)||arrayExpression(canvas.height))return true;
    for(const auto& node:doc.assembly){for(const auto& p:node.p)if(arrayExpression(p.second))return true;if(node.pattern.active)for(const auto* expression:{&node.pattern.count,&node.pattern.dx,&node.pattern.dy,&node.pattern.dz,&node.pattern.step})if(arrayExpression(*expression))return true;}
    for(const auto& mate:doc.mates){if(currentBrepSourceValue(mateToJson(mate))||arrayExpression(mate.value))return true;for(const auto& p:mate.motion)if(arrayExpression(p.second))return true;if(mate.limits.enabled&&(arrayExpression(mate.limits.min)||arrayExpression(mate.limits.max)))return true;}
    return false;
}
bool generation36Document(const Document& doc){
    for(const auto& feature:doc.features)if(feature.type==FeatureType::NativeSolid&&featureOperation(feature)=="extrude"){
        if(feature.p.count("direction")||feature.p.count("symmetric")||feature.nativeSpec.find("direction")||feature.nativeSpec.find("symmetric")||feature.nativeSpec.find("source_face")||feature.nativeSpec.find("limit_face"))return true;
        if(const auto* mode=feature.nativeSpec.find("mode");mode&&mode->asStr()=="up_to_face")return true;
    }return false;
}

bool generation38Document(const Document& doc){
    if(doc.designContainerCheckout.t!=arkjson::Value::Null)return true;
    for(const auto& feature:doc.features)if(feature.type==FeatureType::NativeSolid&&(featureOperation(feature)=="imported_cad"||featureOperation(feature)=="holes_orchestrator"))return true;
    return false;
}
bool generation42Document(const Document& doc){
    auto emission=[](const Value& value){if(value.t!=Value::Obj)return false;for(const auto& field:value.obj)if(field.first.rfind("emission_",0)==0||field.first=="bloom"||field.first=="glow")return true;return false;};
    for(const auto& material:doc.materials)if(emission(appearanceToJson(material.appearance)))return true;
    for(const auto& feature:doc.features){if(feature.type==FeatureType::MeshNet||emission(feature.appearance))return true;for(const auto& face:feature.faceAppearances)if(emission(face.appearance))return true;if(feature.type==FeatureType::NativeSolid&&(featureOperation(feature)=="vent"||featureOperation(feature)=="wood"))return true;}
    return false;
}
bool generation41Document(const Document& doc){
    if(const auto* format=doc.designContainerCheckout.find("format");format&&format->asStr()=="ark.design-container.v2")return true;
    for(const auto& feature:doc.features)if(feature.type==FeatureType::SurfaceEdit){const auto* op=feature.nativeSpec.find("op");if(op&&op->asStr()=="repair")return true;}
    return false;
}
bool generation37Document(const Document& doc){
    for(const auto& feature:doc.features)if(feature.type==FeatureType::NativeSolid&&featureOperation(feature)=="vertex_pull")return true;
    for(const auto& node:doc.assembly)if(node.pattern.active&&(specializedAssemblyPattern(node.pattern)||node.pattern.source.t!=Value::Null||!node.pattern.skips.empty()))return true;
    for(const auto& feature:doc.features)if(feature.type==FeatureType::SheetMetal){
        for(const char* key:{"miters","swept_flanges","corners","rips","tabs"})if(const auto* rows=feature.nativeSpec.find(key);rows&&rows->t==Value::Arr&&!rows->arr.empty())return true;
        if(feature.nativeSpec.find("bend_table"))return true;
    }
    for(const auto& drawing:doc.drawings)if(drawingRequiresVersion37(drawing))return true;
    return false;
}

bool generation34Document(const Document& doc){
    for(const auto& sketch:doc.sketches)for(const auto& entity:sketch.sk.ents)if(entity.kind==SketchEnt::Recipe)return true;
    for(const auto& feature:doc.features)if(feature.type==FeatureType::Curve&&feature.nativeSpec.find("tolerance")){const auto* op=feature.nativeSpec.find("op");if(op&&op->asStr()=="equation")return true;}
    for(const auto& feature:doc.features){if(feature.nativeSpec.find("constraints")||feature.nativeSpec.find("start_ref")||feature.nativeSpec.find("analytic"))return true;if(feature.type==FeatureType::Sweep&&feature.nativeSpec.find("sections"))return true;if(const auto* op=feature.nativeSpec.find("op");op&&op->t==Value::Str&&op->str=="surface_spline")return true;if(const auto* segments=feature.nativeSpec.find("segments"))for(const auto& segment:segments->arr)if(segment.find("to_ref")||segment.find("through_ref")||(segment.find("kind")&&segment.find("kind")->asStr()=="spline"))return true;}
    for(const auto& datum:doc.planes)if(const auto* kind=datum.support.find("kind");kind&&kind->t==Value::Str&&(kind->str=="offset_plane"||kind->str=="angle_plane"||kind->str=="plane_three_points"||kind->str=="plane_axis_point"||kind->str=="plane_point_normal"||kind->str=="axis_points"||kind->str=="axis_planes"||kind->str=="point"||kind->str=="between_points"))return true;
    return false;
}

Value entToJson(const SketchEnt& e){
    Value v=Value::makeObj();
    v.set("kind",Value::makeStr(sketchEntityKindName(e.kind)));
    Value d=Value::makeArr();
    if(e.kind!=SketchEnt::Project&&e.kind!=SketchEnt::Recipe&&e.arcRecipe.t!=Value::Obj)for(size_t i=0;i<e.dof.size();++i)d.arr.push_back(i<e.expressions.size()&&!e.expressions[i].empty()?Value::makeStr(e.expressions[i]):Value::makeNum(e.dof[i]));
    if(e.kind==SketchEnt::Recipe)v.set("recipe",e.entityRecipe);else if(e.arcRecipe.t==Value::Obj)v.set("recipe",e.arcRecipe);else v.set("dof",std::move(d));
    if(!e.name.empty())v.set("name",Value::makeStr(e.name));
    if(e.construction)v.set("construction",Value::makeBool(true));
    if(e.kind==SketchEnt::Spline){v.set("closed",Value::makeBool(e.closed));v.set("tension",Value::makeNum(e.tension));}
    if(e.kind==SketchEnt::Wire||(e.kind==SketchEnt::Offset&&!e.segments.empty())){
        Value segments=Value::makeArr();
        for(const auto& segment:e.segments){
            Value item=Value::makeObj();
            item.set("kind",Value::makeStr(segment.kind==SketchEnt::WireSeg::Line?"line":segment.kind==SketchEnt::WireSeg::Arc?"arc":"cubic"));
            if(segment.kind==SketchEnt::WireSeg::Arc) item.set("sweep",Value::makeNum(segment.sweep));
            segments.arr.push_back(std::move(item));
        }
        v.set("segments",std::move(segments));
    }
    if(e.kind==SketchEnt::Project){if(e.sourceSketch.empty()){v.set("feature",Value::makeNum((double)e.sourceFeature));v.set("reference",Value::makeStr(e.reference));}else{v.set("source_sketch",Value::makeStr(e.sourceSketch));v.set("source_entity",Value::makeStr(e.sourceEntity));}if(e.projectionMode!="legacy")v.set("mode",Value::makeStr(e.projectionMode));if(e.sourceInstance){v.set("source_instance",Value::makeNum(e.sourceInstance));v.set("source_copy",Value::makeNum(e.sourceCopy));}}
    if(e.kind==SketchEnt::Offset){v.set("source",Value::makeStr(e.sourceName));v.set("distance",Value::makeStr(e.distance));}
    return v;
}

bool entFromJson(const Value& v, SketchEnt& e, std::string* err){
    auto fail=[&](const std::string& m){if(err)*err=m;return false;};
    auto take=[&](const Value& x){
        if(x.t==Value::Str&&!x.str.empty()&&x.str.size()<=2048){e.dof.push_back(0);e.expressions.push_back(x.str);return true;}
        if(x.t==Value::Num&&std::isfinite(x.num)&&std::abs(x.num)<=1e7){e.dof.push_back(x.num);e.expressions.emplace_back();return true;}
        return fail("dof must be finite numbers or nonempty expressions");
    };
    auto takePair=[&](const Value* pair,const char* what){
        if(!pair||pair->t!=Value::Arr||pair->arr.size()!=2) return fail(std::string(what)+" must be [x,y]");
        return take(pair->arr[0])&&take(pair->arr[1]);
    };
    if(v.t!=Value::Obj)return fail("Sketch entity must be object");
    std::string k=v.find("kind")?v.find("kind")->asStr("point"):"point";
    if(k=="point") e.kind=SketchEnt::Pnt;
    else if(k=="line") e.kind=SketchEnt::Line;
    else if(k=="circle") e.kind=SketchEnt::Circle;
    else if(k=="spline") e.kind=SketchEnt::Spline;
    else if(k=="arc") e.kind=SketchEnt::Arc;
    else if(k=="wire") e.kind=SketchEnt::Wire;
    else if(k=="project") e.kind=SketchEnt::Project;
    else if(k=="offset") e.kind=SketchEnt::Offset;
    else if(k=="recipe") e.kind=SketchEnt::Recipe;
    else { if(err)*err="unknown sketch entity kind: "+k; return false; }
    if(auto recipe=v.find("recipe")){
        std::string reason;if(e.kind==SketchEnt::Recipe){if(!validateSketchEntityRecipe(*recipe,reason))return fail(reason);if(const auto* dof=v.find("dof");dof&&(dof->t!=Value::Arr||!dof->arr.empty()))return fail("Recipe source and resolved dof are exclusive");e.entityRecipe=*recipe;}else {if(e.kind!=SketchEnt::Arc)return fail("Only an arc or typed entity accepts a construction recipe");
        if(!validateSketchArcRecipe(*recipe,reason))return fail(reason);
        if(auto dof=v.find("dof");dof&&(dof->t!=Value::Arr||!dof->arr.empty()))return fail("Arc recipe and center/angle dof are exclusive");
        e.arcRecipe=*recipe;}
    }
    if(e.kind==SketchEnt::Recipe&&e.entityRecipe.t!=Value::Obj)return fail("Typed sketch entity needs recipe");
    const bool driven=e.kind==SketchEnt::Project||e.kind==SketchEnt::Offset;
    const bool recipeDriven=e.kind==SketchEnt::Recipe;
    if(e.kind==SketchEnt::Wire||driven){
        auto segments=v.find("segments");
        if(e.kind==SketchEnt::Wire){if(!segments||segments->t!=Value::Arr||segments->arr.empty()||segments->arr.size()>64) return fail("Wire needs 1..64 segments: {kind:\"wire\",start:[x,y],segments:[{kind:\"line\",to:[x,y]}|{kind:\"arc\",to:[x,y],radius:r,sweep?:1|-1}|{kind:\"cubic\",c1:[x,y],c2:[x,y],to:[x,y]}]}");}
        else if(segments&&(segments->t!=Value::Arr||segments->arr.size()>64)) return fail("Projected or offset geometry exceeds 64 segments");
        if(segments&&segments->t==Value::Arr)for(const auto& segment:segments->arr){
            if(segment.t!=Value::Obj) return fail("Wire segment must be an object");
            auto kind=segment.find("kind");if(!kind||kind->t!=Value::Str) return fail("Wire segment needs a kind");
            SketchEnt::WireSeg item;
            if(kind->str=="line") item.kind=SketchEnt::WireSeg::Line;
            else if(kind->str=="arc") item.kind=SketchEnt::WireSeg::Arc;
            else if(kind->str=="cubic") item.kind=SketchEnt::WireSeg::Cubic;
            else return fail("Wire segment kind must be line, arc or cubic");
            if(item.kind==SketchEnt::WireSeg::Arc){
                item.sweep=1;
                if(auto sweep=segment.find("sweep")){if(sweep->t!=Value::Num||(sweep->num!=1&&sweep->num!=-1)) return fail("Wire arc sweep must be +1 or -1");item.sweep=(int)sweep->num;}
            }
            e.segments.push_back(item);
        }
    }
    if(auto d=v.find("dof")){
        if(d->t!=Value::Arr){if(err)*err="dof must be array";return false;}
        for(auto&x:d->arr) if(!take(x)) return false;
    }
    if(e.kind==SketchEnt::Wire&&e.dof.empty()){
        if(!takePair(v.find("start"),"Wire start")) return false;
        for(size_t i=0;i<e.segments.size();++i){
            const auto& segment=v.find("segments")->arr[i];
            const auto& item=e.segments[i];
            if(item.kind==SketchEnt::WireSeg::Line){ if(!takePair(segment.find("to"),"Wire line")) return false; }
            else if(item.kind==SketchEnt::WireSeg::Arc){
                if(!takePair(segment.find("to"),"Wire arc")) return false;
                auto radius=segment.find("radius");if(!radius||!take(*radius)) return fail("Wire arc needs a radius");
            }else{
                if(!takePair(segment.find("c1"),"Wire cubic control")||!takePair(segment.find("c2"),"Wire cubic control")||!takePair(segment.find("to"),"Wire cubic")) return false;
            }
        }
    }
    if(e.dof.empty()){
        if(e.kind==SketchEnt::Pnt) e.dof={0,0};
        else if(e.kind==SketchEnt::Line) e.dof={0,0,10,0};
        else if(e.kind==SketchEnt::Circle)e.dof={0,0,5};
        else if(e.kind==SketchEnt::Arc) e.dof={0,0,5,0,180};
        else if(e.kind==SketchEnt::Wire) return fail("Wire requires start and segment endpoints");
        else if(!driven&&!recipeDriven) return fail("Spline requires explicit anchor coordinates");
    }
    if(e.kind==SketchEnt::Wire||(driven&&!e.segments.empty())){
        int expect=2;for(const auto& segment:e.segments) expect+=segment.kind==SketchEnt::WireSeg::Line?2:segment.kind==SketchEnt::WireSeg::Arc?3:6;
        if((int)e.dof.size()!=expect) return fail(driven?"Projected or offset degrees of freedom do not match segments":"Wire degrees of freedom do not match segments");
    }else if(!driven&&!recipeDriven&&e.kind!=SketchEnt::Spline){
        const size_t expected=e.kind==SketchEnt::Pnt?2:e.kind==SketchEnt::Line?4:e.kind==SketchEnt::Arc?5:3;
        if(e.dof.size()!=expected)return fail("wrong entity degrees of freedom");
    }
    if(auto n=v.find("name")){if(n->t!=Value::Str||n->str.empty()||n->str.size()>128)return fail("entity name must be 1..128 characters");e.name=n->str;}
    for(auto pair:{std::pair{"construction",&e.construction},std::pair{"closed",&e.closed}})if(auto b=v.find(pair.first)){if(b->t!=Value::Bool)return fail("entity flags must be boolean");*pair.second=b->b;}
    if(auto t=v.find("tension")){if(t->t!=Value::Num||!std::isfinite(t->num)||t->num<0||t->num>1)return fail("Spline tension must be 0..1");e.tension=t->num;}
    if(e.kind==SketchEnt::Spline&&(e.dof.size()<(e.closed?6u:4u)||e.dof.size()>128||e.dof.size()%2))return fail("Spline requires 2..64 XY anchors; closed requires at least 3");
    if(e.kind==SketchEnt::Project){
        if(auto sketch=v.find("source_sketch")){
            auto entity=v.find("source_entity");if(sketch->t!=Value::Str||sketch->str.empty()||sketch->str.size()>128||!entity||entity->t!=Value::Str||entity->str.empty()||entity->str.size()>128||v.find("feature")||v.find("reference")||v.find("source_instance")||v.find("source_copy"))return fail("Choose one named sketch/entity source or one feature/reference source");
            e.sourceSketch=sketch->str;e.sourceEntity=entity->str;e.projectionMode="normal";
        }else{
        auto feature=v.find("feature");
        if(!feature||feature->t!=Value::Num||!std::isfinite(feature->num)||feature->num<1||feature->num>9007199254740990.||feature->num!=std::floor(feature->num)) return fail("Project needs a feature id");
        e.sourceFeature=(u64)feature->num;
        auto reference=v.find("reference");
        if(!reference||reference->t!=Value::Str||reference->str.empty()||reference->str.size()>128) return fail("Project needs a reference");
        e.reference=reference->str;
        if(v.find("source_entity"))return fail("Projection source_entity requires source_sketch");
        }
        if(auto mode=v.find("mode")){if(mode->t!=Value::Str||mode->str!="normal")return fail("2D projection mode must be normal");e.projectionMode=mode->str;}
        if(auto instance=v.find("source_instance")){if(instance->t!=Value::Num||instance->num<1||instance->num>9007199254740990.||instance->num!=std::floor(instance->num))return fail("Projection source_instance must be a positive node id");e.sourceInstance=(u64)instance->num;}
        if(auto copy=v.find("source_copy")){if(!e.sourceInstance||copy->t!=Value::Num||copy->num<0||copy->num>31||copy->num!=std::floor(copy->num))return fail("Projection source_copy needs an instance and integer 0..31");e.sourceCopy=(int)copy->num;}
    }
    if(e.kind==SketchEnt::Offset){
        auto source=v.find("source");if(!source) source=v.find("entity");
        if(!source||source->t!=Value::Str||source->str.empty()||source->str.size()>128) return fail("Offset needs a source entity");
        e.sourceName=source->str;
        auto distance=v.find("distance");
        if(!distance) return fail("Offset needs a distance");
        if(distance->t==Value::Str&&!distance->str.empty()&&distance->str.size()<=2048) e.distance=distance->str;
        else if(distance->t==Value::Num&&std::isfinite(distance->num)) e.distance=distance->dump();
        else return fail("Offset distance must be a number or expression");
    }
    return true;
}

bool cornerFromJson(const Value& v,SketchCorner& corner,std::string* err){
    auto fail=[&](const char* message){if(err)*err=message;return false;};
    if(v.t!=Value::Obj)return fail("Sketch corner must be an object");
    auto entity=v.find("entity");
    if(!entity||entity->t!=Value::Str||entity->str.empty()||entity->str.size()>128)return fail("Sketch entity not found");
    corner.entity=entity->str;
    auto index=v.find("corner");
    if(!index||index->t!=Value::Num||!std::isfinite(index->num)||index->num<0||index->num>4096||index->num!=std::floor(index->num))return fail("Fillet corner is not between two edges");
    corner.corner=(int)index->num;
    auto kind=v.find("kind");
    if(!kind||kind->t!=Value::Str||(kind->str!="fillet"&&kind->str!="chamfer"))return fail("Sketch corner kind must be fillet or chamfer");
    corner.chamfer=kind->str=="chamfer";
    auto size=v.find("size");
    if(!size)return fail(corner.chamfer?"Chamfer distance must be a positive length":"Fillet radius must be a positive length");
    if(size->t==Value::Str&&!size->str.empty()&&size->str.size()<=2048)corner.size=size->str;
    else if(size->t==Value::Num&&std::isfinite(size->num))corner.size=size->dump();
    else return fail(corner.chamfer?"Chamfer distance must be a positive length":"Fillet radius must be a positive length");
    return true;
}

bool validSketch(const Sketch& sk,std::string* err){
    auto fail=[&](const char* e){if(err)*err=e;return false;};
    if(sk.ents.size()>128||sk.cons.size()>256)return fail("sketch capacity exceeded");
    size_t variables=0;std::set<std::string> names;
    for(auto& e:sk.ents){if(!e.name.empty()&&!names.insert(e.name).second)return fail("duplicate sketch entity name");variables+=e.dof.size();}
    if(variables>512)return fail("Sketch exceeds 512 scalar coordinates");
    std::set<std::string> constraintIds;
    for(auto& c:sk.cons){
        if(!c.id.empty()&&!constraintIds.insert(c.id).second)return fail("Duplicate sketch constraint id");
        auto slots=[&](const SketchEnt& entity){return entity.kind==SketchEnt::Spline?(int)entity.dof.size()/2:entity.kind==SketchEnt::Line?2:(entity.kind==SketchEnt::Wire||entity.kind==SketchEnt::Project||entity.kind==SketchEnt::Offset)?sketchVertexCount(entity):1;};
        auto valid=[&](int e,int slot){return e>=0&&(size_t)e<sk.ents.size()&&slot>=0&&slot<slots(sk.ents[e]);};
        if(!valid(c.a,c.pa))return fail("invalid constraint entity or point slot");
        bool pair=c.type==SketchCon::DistPP||c.type==SketchCon::Coincident||c.type==SketchCon::Parallel||c.type==SketchCon::Perpendicular||c.type==SketchCon::AngleLL||c.type==SketchCon::Tangent;
        if(pair&&!valid(c.b,c.pb))return fail("invalid second constraint entity");
        bool lines=c.type==SketchCon::Parallel||c.type==SketchCon::Perpendicular||c.type==SketchCon::AngleLL;
        if((lines||c.type==SketchCon::Horizontal||c.type==SketchCon::Vertical)&&sk.ents[c.a].kind!=SketchEnt::Line)return fail("constraint requires line");
        if(lines&&sk.ents[c.b].kind!=SketchEnt::Line)return fail("constraint requires two lines");
        if(c.type==SketchCon::Radius&&sk.ents[c.a].kind!=SketchEnt::Circle&&sk.ents[c.a].kind!=SketchEnt::Arc)return fail("radius requires circle or arc");
        if(c.type==SketchCon::Tangent){
            auto round=[&](int index){auto kind=sk.ents[(size_t)index].kind;return kind==SketchEnt::Circle||kind==SketchEnt::Arc;};
            bool lineA=sk.ents[(size_t)c.a].kind==SketchEnt::Line,lineB=sk.ents[(size_t)c.b].kind==SketchEnt::Line;
            if(!((lineA&&round(c.b))||(lineB&&round(c.a)))) return fail("tangent requires one line and one circle or arc");
        }
    }
    if(sk.corners.size()>64)return fail("Sketch corner capacity exceeded");
    std::set<std::pair<std::string,int>> cornerIds;
    for(const auto& corner:sk.corners){
        if(!cornerIds.insert({corner.entity,corner.corner}).second)return fail("Duplicate sketch corner");
        const SketchEnt* found=nullptr;
        for(const auto& entity:sk.ents)if(entity.name==corner.entity){
            if(found)return fail("Ambiguous sketch entity");
            found=&entity;
        }
        if(!found)return fail("Sketch entity not found");
        if(found->kind!=SketchEnt::Wire)return fail("Fillet a wire corner");
        if(corner.size.empty())return fail(corner.chamfer?"Chamfer distance must be a positive length":"Fillet radius must be a positive length");
    }
    return true;
}

Value conToJson(const SketchCon& c){
    Value v=Value::makeObj();
    if(!c.id.empty())v.set("id",Value::makeStr(c.id));
    v.set("type",Value::makeStr(constraintTypeName(c.type)));
    v.set("a",Value::makeNum(c.a));
    v.set("b",Value::makeNum(c.b));
    v.set("pa",Value::makeNum(c.pa));
    v.set("pb",Value::makeNum(c.pb));
    v.set("val",c.expression.empty()?Value::makeNum(c.val):Value::makeStr(c.expression));
    v.set("val2",c.expression2.empty()?Value::makeNum(c.val2):Value::makeStr(c.expression2));
    return v;
}

bool conFromJson(const Value& v, SketchCon& c, std::string* err){
    auto fail=[&](const char* m){if(err)*err=m;return false;};
    if(v.t!=Value::Obj)return fail("Constraint must be object");
    if(auto key=v.find("id")){if(key->t!=Value::Str||key->str.empty()||key->str.size()>128||key->str.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)return fail("Constraint id needs 1..128 letters, numbers, dot, underscore or hyphen");c.id=key->str;}
    i32 t=0;
    std::string tn=v.find("type")?v.find("type")->asStr():"";
    if(!constraintTypeFromName(tn,t)){ if(err)*err="unknown constraint type: "+tn; return false; }
    c.type=(SketchCon::Type)t;
    for(auto pair:{std::pair{"a",&c.a},std::pair{"b",&c.b},std::pair{"pa",&c.pa},std::pair{"pb",&c.pb}})if(auto p=v.find(pair.first)){
        if(p->t!=Value::Num||!std::isfinite(p->num)||p->num< -1||p->num>512||p->num!=std::floor(p->num))return fail("Constraint indexes must be integers");
        *pair.second=(int)p->num;
    }
    for(auto key:{"val","val2"})if(auto p=v.find(key)){
        auto& expr=std::string(key)=="val"?c.expression:c.expression2;auto& number=std::string(key)=="val"?c.val:c.val2;
        if(p->t==Value::Str&&!p->str.empty()&&p->str.size()<=2048)expr=p->str;
        else if(p->t==Value::Num&&std::isfinite(p->num))number=p->num;
        else return fail("Dimension must be finite number or expression");
    }
    return true;
}

bool onePointSketch(const Document& doc, const std::string& name){
    const NamedSketch* sketch=nullptr;
    for(const auto& candidate:doc.sketches) if(candidate.name==name) sketch=&candidate;
    if(!sketch) return false;
    int points=0, edges=0;
    for(const auto& entity:sketch->sk.ents){
        if(entity.construction) continue;
        if(entity.kind==SketchEnt::Pnt) ++points;
        else ++edges;
    }
    return points==1 && edges==0;
}

bool generation9Feature(const Document& doc, const Feature& feature){
    auto enabled=[&](const char* key){auto it=feature.p.find(key);return it!=feature.p.end()&&it->second!="0"&&it->second!="0.0"&&!it->second.empty();};
    if(feature.type==FeatureType::Loft){
        if(enabled("smooth")||enabled("compatible")) return true;
        auto continuity=feature.p.find("continuity");
        if(continuity!=feature.p.end()&&continuity->second!="1") return true;
        if(const auto* sections=feature.nativeSpec.find("sections"); sections&&sections->t==Value::Arr)
            for(const auto& name:sections->arr) if(name.t==Value::Str&&onePointSketch(doc,name.str)) return true;
    }
    if(feature.type==FeatureType::NativeSolid){
        std::string op=feature.get("op");
        if(op.empty()) if(const auto* authored=feature.nativeSpec.find("op"); authored&&authored->t==Value::Str) op=authored->str;
        if(op=="draft"||op=="linear_pattern"||op=="circular_pattern"||op=="mirror") return true;
        if(feature.nativeSpec.find("selection")) return true;
        if(feature.p.count("distance2")) return true;
    }
    return false;
}

bool namesAuthoredCurve(const Document& doc,const std::string& name,bool allowPoint){
    for(const auto& sketch:doc.sketches) if(sketch.name==name) return false;
    if(allowPoint) for(const auto& plane:doc.planes) if(plane.name==name&&plane.kind=="point") return true;
    int count=0;bool curve=false;
    for(const auto& feature:doc.features) if(feature.name==name){++count;curve=isCurveFeature(feature.type);}
    return count==1&&curve;
}
bool namesAuthoredProfile(const Document& doc,const Value& value,bool allowPoint){
    if(value.t==Value::Str) return namesAuthoredCurve(doc,value.str,allowPoint);
    if(value.t==Value::Arr) for(const auto& name:value.arr) if(name.t==Value::Str&&namesAuthoredCurve(doc,name.str,allowPoint)) return true;
    return false;
}
bool generation12Document(const Document& doc){
    for(const auto& plane:doc.planes){
        if(plane.kind=="point") return true;
        if(plane.support.t==Value::Obj) if(const auto* kind=plane.support.find("kind"); kind&&kind->t==Value::Str&&(kind->str=="on_curve"||kind->str=="on_face")) return true;
    }
    for(const auto& sketch:doc.sketches){
        for(const auto& entity:sketch.sk.ents) if(entity.kind==SketchEnt::Arc||entity.kind==SketchEnt::Wire) return true;
        for(const auto& constraint:sketch.sk.cons) if(constraint.type==SketchCon::Tangent) return true;
    }
    for(const auto& feature:doc.features){
        if(isCurveFeature(feature.type)) return true;
        if(feature.type==FeatureType::Loft&&(namesAuthoredProfile(doc,feature.nativeSpec.find("sections")?*feature.nativeSpec.find("sections"):Value{},true)||namesAuthoredProfile(doc,feature.nativeSpec.find("guides")?*feature.nativeSpec.find("guides"):Value{},false))) return true;
        if(feature.type==FeatureType::Sweep&&(namesAuthoredProfile(doc,feature.nativeSpec.find("section")?*feature.nativeSpec.find("section"):Value{},false)||namesAuthoredProfile(doc,feature.nativeSpec.find("path")?*feature.nativeSpec.find("path"):Value{},false))) return true;
    }
    return false;
}
bool generation13Document(const Document& doc){
    if(!doc.canvases.empty()) return true;
    for(const auto& feature:doc.features) if(feature.suppressed) return true;
    for(const auto& sketch:doc.sketches) for(const auto& entity:sketch.sk.ents) if(entity.kind==SketchEnt::Project||entity.kind==SketchEnt::Offset) return true;
    return false;
}
bool generation14Document(const Document& doc){
    for(const auto& sketch:doc.sketches) if(!sketch.sk.corners.empty()) return true;
    for(const auto& mate:doc.mates){
        bool foundA=false,foundB=false;u64 parentA=0,parentB=0;
        for(const auto& node:doc.assembly){
            if(node.id==mate.a){foundA=true;parentA=node.parent;}
            if(node.id==mate.b){foundB=true;parentB=node.parent;}
        }
        if(foundA&&foundB&&parentA!=parentB) return true;
    }
    return false;
}
bool generation15NativeFeature(const Feature& feature){
    if(feature.type!=FeatureType::NativeSolid)return false;
    std::string op=feature.get("op");
    if(op.empty())if(const auto* authored=feature.nativeSpec.find("op");authored&&authored->t==Value::Str)op=authored->str;
    return op=="split_plane"||(op=="mirror"&&feature.p.count("copy_only")!=0);
}
bool generation17SheetOpening(const Feature& feature){
    if(feature.type!=FeatureType::SheetMetal)return false;
    const Value* openings=feature.nativeSpec.find("openings");if(!openings||openings->t!=Value::Arr)return false;
    for(const Value& opening:openings->arr)if(const Value* kind=opening.find("kind");kind&&kind->t==Value::Str&&(kind->str=="slot"||kind->str=="window"))return true;
    return false;
}

bool generation11Document(const Document& doc){
    for(const auto& feature:doc.features){
        if(feature.type==FeatureType::Loft){
            if(const auto* guides=feature.nativeSpec.find("guides"); guides&&guides->t==Value::Arr&&!guides->arr.empty()) return true;
        }
        if(feature.type==FeatureType::NativeSolid){
            std::string op=feature.get("op");
            if(op.empty()) if(const auto* authored=feature.nativeSpec.find("op"); authored&&authored->t==Value::Str) op=authored->str;
            if(op=="shell") return true;
        }
    }
    return false;
}

bool generation10Document(const Document& doc){
    for(const auto& node:doc.assembly) if(node.pattern.active) return true;
    for(const auto& feature:doc.features){
        if(feature.type!=FeatureType::NativeSolid) continue;
        if(feature.nativeSpec.find("instances")) return true;
        std::string op=feature.get("op");
        if(op.empty()) if(const auto* authored=feature.nativeSpec.find("op"); authored&&authored->t==Value::Str) op=authored->str;
        if((op=="union"||op=="cut"||op=="common"||op=="boolean_union"||op=="boolean_cut"||op=="boolean_common"||op=="intersect") && feature.inputs.size()>2) return true;
    }
    return false;
}

bool arcRecipeDocument(const Document& doc){
    for(const auto& sketch:doc.sketches)for(const auto& entity:sketch.sk.ents)if(entity.arcRecipe.t==Value::Obj)return true;
    for(const auto& feature:doc.features)if(feature.type==FeatureType::NativeSolid&&featureOperation(feature)=="tube_end_fit")return true;
    for(const auto& feature:doc.features)if(feature.type==FeatureType::Curve)if(auto segments=feature.nativeSpec.find("segments"))for(const auto& segment:segments->arr)if(auto kind=segment.find("kind");kind&&kind->str=="arc")if(segment.find("mode")||segment.find("major"))return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
std::string documentToJson(const Document& doc,bool includeAudit,bool includeHistory){
    std::string usageError;if(!validateUsageLedger(doc.usage,usageError))throw std::runtime_error(usageError);
    validateAppearanceAuthoring024B(doc,false);
    Value r=Value::makeObj();
    r.set("format",Value::makeStr("ark"));
    bool sourceTube=false;bool notched=false;bool nativeVersion=false;bool generation8=false;bool generation9=false;bool extended=!doc.assembly.empty()||doc.nextAssemblyId_>1;for(auto& f:doc.features){extended|=f.type==FeatureType::Tube;notched|=f.type==FeatureType::NotchedTube;sourceTube|=f.type==FeatureType::SourceTube;nativeVersion|=f.type==FeatureType::Loft||f.type==FeatureType::SurfaceFill||f.type==FeatureType::BSplineSurface||f.type==FeatureType::Thicken||f.type==FeatureType::SheetMetal||f.type==FeatureType::SourceSheet||f.type==FeatureType::NativeSolid;generation8|=f.type==FeatureType::Sweep||f.type==FeatureType::ExtrudeSurface||f.type==FeatureType::RevolveSurface||f.type==FeatureType::OffsetSurface||f.type==FeatureType::Sew||isMeshModelFeature(f.type);generation9|=generation9Feature(doc,f);}
    bool generation14=generation14Document(doc);
    bool generation13=generation13Document(doc);
    bool generation12=generation12Document(doc);
    bool generation11=generation11Document(doc);
    bool generation10=generation10Document(doc);
    bool sketchVersion=!doc.planes.empty()||!doc.sketches.empty();for(auto& f:doc.features)sketchVersion|=f.type==FeatureType::SketchCut;
    bool latest=!doc.mates.empty()||doc.nextMateId_>1;for(auto& p:doc.planes)latest|=p.support.t==Value::Obj||p.kind!="plane";for(auto& f:doc.features)latest|=f.type==FeatureType::NativeSolid||(f.type==FeatureType::SheetMetal&&f.nativeSpec.find("base"));for(auto& n:doc.assembly)latest|=n.fixed;for(auto& sk:doc.sketches)for(auto& c:sk.sk.cons)latest|=!c.id.empty();
    bool generation15=!doc.materials.empty();for(const auto& feature:doc.features)generation15|=!feature.material.empty()||feature.appearance.t==Value::Obj||generation15NativeFeature(feature);
    bool modules=false;for(const auto& feature:doc.features)modules|=feature.type==FeatureType::ConstructionFrame||feature.type==FeatureType::PCB||feature.type==FeatureType::CableRoute;
    const bool usagePresent=includeAudit&&(doc.usage.serial>0||!doc.usage.entries.empty());
    bool generation17=usagePresent;for(const auto& feature:doc.features)generation17|=generation17SheetOpening(feature);
    bool suspensionVersion=false;for(const auto& feature:doc.features)suspensionVersion|=feature.type==FeatureType::Suspension;
    bool patternVersion=false;for(const auto& f:doc.features)if(f.type==FeatureType::NativeSolid){auto op=f.nativeSpec.find("op");patternVersion|=(op&&(op->str=="curve_pattern"||op->str=="hole_pattern"))||f.get("op")=="curve_pattern"||f.get("op")=="hole_pattern";}
    bool physicsVersion=false;for(const auto& m:doc.materials)physicsVersion|=m.physical.t==Value::Obj&&!m.physical.obj.empty();
    bool sheetVersion=false;for(const auto& f:doc.features)sheetVersion|=!f.manufacturingRoute.empty()||(f.type==FeatureType::SheetMetal&&((f.nativeSpec.find("hems")&&!f.nativeSpec.find("hems")->arr.empty())||(f.nativeSpec.find("stamps")&&!f.nativeSpec.find("stamps")->arr.empty())));
    bool mechanicalVersion=false;for(const auto& mate:doc.mates)mechanicalVersion|=isMechanicalMate(mate.type);
    bool processVersion=false;for(const auto& feature:doc.features)processVersion|=feature.manufacturingRoute=="injection_molding"||feature.manufacturingRoute=="additive"||(feature.manufacturingProcess.t==Value::Obj&&!feature.manufacturingProcess.obj.empty());
    bool componentVersion=false;for(const auto& feature:doc.features)componentVersion|=feature.type==FeatureType::NativeSolid&&featureOperation(feature)=="imported_brep";
    bool fabricationVersion=false;for(const auto& feature:doc.features){if(feature.type==FeatureType::SheetMetal)fabricationVersion|=feature.nativeSpec.find("jog")!=nullptr||feature.nativeSpec.find("sketch_bend")!=nullptr;if(feature.type==FeatureType::ConstructionFrame){if(auto welds=feature.nativeSpec.find("welds"))fabricationVersion|=welds->t==Value::Arr&&!welds->arr.empty();if(auto joints=feature.nativeSpec.find("joints"))for(const auto& joint:joints->arr)if(auto kind=joint.find("kind"))fabricationVersion|=kind->str=="trim_extend"||kind->str=="trim_plane";}}
    bool surfaceEditVersion=false;for(const auto& feature:doc.features)surfaceEditVersion|=feature.type==FeatureType::SurfaceEdit;
    r.set("version",Value::makeNum(surfaceEditVersion?26:fabricationVersion?25:componentVersion?24:processVersion?23:mechanicalVersion?22:sheetVersion?21:physicsVersion?20:patternVersion?19:suspensionVersion?18:generation17?17:modules?16:generation15?15:generation14?14:generation13?13:generation12?12:generation11?11:generation10?10:generation9?9:generation8?8:latest?7:nativeVersion?6:sketchVersion?5:sourceTube?4:notched?3:extended?2:1));
    bool projectionVersion=false;
    for(const auto& sketch:doc.sketches)for(const auto& entity:sketch.sk.ents)if(entity.kind==SketchEnt::Project&&(entity.projectionMode!="legacy"||entity.sourceInstance||!entity.sourceSketch.empty()))projectionVersion=true;
    for(const auto& feature:doc.features)if(feature.type==FeatureType::Curve)if(auto op=feature.nativeSpec.find("op");op&&op->str=="project")projectionVersion=true;
    if(projectionVersion)r.set("version",Value::makeNum(28));
    for(const auto& mate:doc.mates)if(mate.type=="rigid"||mate.type=="revolute")r.set("version",Value::makeNum(29));
    if(arcRecipeDocument(doc))r.set("version",Value::makeNum(30));
    for(const auto& mate:doc.mates)if(mate.type=="prismatic"||mate.limits.enabled)r.set("version",Value::makeNum(31));
    bool bomVersion=!doc.bomDocumentId.empty();for(const auto& feature:doc.features)bomVersion|=feature.part.t==Value::Obj;
    if(bomVersion){r.set("version",Value::makeNum(32));r.set("bom_document_id",Value::makeStr(doc.bomDocumentId));}
    r.set("units",Value::makeStr("mm"));
    r.set("next_feature_id",Value::makeNum((double)doc.nextFeatureId_));

    Value ps=Value::makeArr();
    for(auto&p:doc.params){
        Value v=Value::makeObj();
        v.set("name",Value::makeStr(p.name));
        v.set("expr",Value::makeStr(p.expr));
        ps.arr.push_back(std::move(v));
    }
    r.set("params",std::move(ps));
    if(!doc.imageAssets.empty()){Value assets=Value::makeArr();for(const auto& asset:doc.imageAssets)assets.arr.push_back(imageAssetToJson(asset,true));r.set("image_assets",assets);}
    if(!doc.stamps.empty()){Value stamps=Value::makeArr();for(const auto& stamp:doc.stamps)stamps.arr.push_back(stampToJson(stamp));r.set("stamps",stamps);}
    if(usagePresent)r.set("usage",usageLedgerToJson(doc.usage));
    if(!doc.materials.empty()){
        Value materials=Value::makeArr();
        for(const auto& material:doc.materials){Value item=Value::makeObj();item.set("name",Value::makeStr(material.name));item.set("density",Value::makeNum(material.densityGcm3));item.set("appearance",appearanceToJson(material.appearance));if(material.physical.t==Value::Obj&&!material.physical.obj.empty())item.set("physical",material.physical);materials.arr.push_back(std::move(item));}
        r.set("materials",std::move(materials));
    }
    if(!doc.planes.empty()){
        Value planes=Value::makeArr();for(auto& plane:doc.planes){Value v=Value::makeObj(),p=Value::makeObj();v.set("name",Value::makeStr(plane.name));v.set("parent",Value::makeStr(plane.parent));
            std::vector<std::string> keys;for(auto& kv:plane.p)keys.push_back(kv.first);std::sort(keys.begin(),keys.end());for(auto& key:keys)p.set(key,Value::makeStr(plane.p.at(key)));v.set("p",p);v.set("kind",Value::makeStr(plane.kind));if(plane.support.t==Value::Obj)v.set("support",plane.support);planes.arr.push_back(v);
        }r.set("planes",planes);
    }
    if(!doc.canvases.empty()){
        Value canvases=Value::makeArr();
        for(auto& canvas:doc.canvases){
            Value item=Value::makeObj();
            item.set("name",Value::makeStr(canvas.name));item.set("plane",Value::makeStr(canvas.plane));item.set("path",Value::makeStr(canvas.path));
            if(!canvas.asset.empty())item.set("asset",Value::makeStr(canvas.asset));
            item.set("width",Value::makeStr(canvas.width));item.set("height",Value::makeStr(canvas.height));item.set("opacity",Value::makeNum(canvas.opacity));
            canvases.arr.push_back(std::move(item));
        }
        r.set("canvases",canvases);
    }

    Value sks=Value::makeArr();
    for(auto&ns:doc.sketches){
        Value v;arkjson::parse(sketchToJson(ns.sk),v);
        v.set("name",Value::makeStr(ns.name));
        Value es=Value::makeArr();   for(auto&e:ns.sk.ents) es.arr.push_back(entToJson(e));
        Value cs=Value::makeArr();   for(auto&c:ns.sk.cons) cs.arr.push_back(conToJson(c));
        v.set("entities",std::move(es));
        v.set("constraints",std::move(cs));
        sks.arr.push_back(std::move(v));
    }
    r.set("sketches",std::move(sks));

    Value fs=Value::makeArr();
    for(auto&f:doc.features){
        Value v=Value::makeObj();
        v.set("id",Value::makeNum((double)f.id));
        v.set("type",Value::makeStr(featureTypeName(f.type)));
        v.set("name",Value::makeStr(f.name));
        if(f.part.t==Value::Obj)v.set("part",f.part);
        if(!f.manufacturingRoute.empty()){Value manufacturing=Value::makeObj();manufacturing.set("route",Value::makeStr(f.manufacturingRoute));if(f.manufacturingProcess.t==Value::Obj&&!f.manufacturingProcess.obj.empty())manufacturing.set("process",f.manufacturingProcess);v.set("manufacturing",manufacturing);}
        v.set("op",Value::makeNum(f.opInt));
        if(f.suppressed)v.set("suppressed",Value::makeBool(true));
        if(!f.material.empty())v.set("material",Value::makeStr(f.material));
        if(f.appearance.t==Value::Obj)v.set("appearance",f.appearance);
        if(!f.faceAppearances.empty()){Value faces=Value::makeArr();for(const auto& appearance:f.faceAppearances)faces.arr.push_back(faceAppearanceToJson(appearance));v.set("face_appearances",faces);}
        Value in=Value::makeArr();
        for(u64 id:f.inputs) in.arr.push_back(Value::makeNum((double)id));
        v.set("inputs",std::move(in));
        Value pv=Value::makeObj();
        std::vector<std::string> keys; for(auto&kv:f.p)keys.push_back(kv.first);
        std::sort(keys.begin(),keys.end());
        for(auto& key:keys) pv.set(key,Value::makeStr(f.p.at(key)));
        v.set("p",std::move(pv));
        if(f.type==FeatureType::Tube){Value bends=Value::makeArr();for(auto& b:f.bends){Value j=Value::makeObj();j.set("feed",Value::makeStr(b.feed));j.set("radius",Value::makeStr(b.radius));j.set("angle",Value::makeStr(b.angle));j.set("roll",Value::makeStr(b.roll));bends.arr.push_back(j);}v.set("bends",bends);}
        if(f.type==FeatureType::NotchedTube){Value points=Value::makeArr();for(auto& p:f.notches){Value j=Value::makeObj();j.set("station",Value::makeStr(p.station));j.set("angle",Value::makeStr(p.angle));j.set("face",Value::makeStr(p.face));points.arr.push_back(j);}v.set("notches",points);}
        if(f.type==FeatureType::SourceTube)v.set("source",f.sourceData);
        if(f.type==FeatureType::SourceSheet)v.set("source",f.sourceData);
        if(f.type==FeatureType::Loft||f.type==FeatureType::SurfaceFill||f.type==FeatureType::BSplineSurface||f.type==FeatureType::Thicken||f.type==FeatureType::SheetMetal||f.type==FeatureType::NativeSolid||f.type==FeatureType::Sweep||f.type==FeatureType::ExtrudeSurface||f.type==FeatureType::RevolveSurface||f.type==FeatureType::OffsetSurface||f.type==FeatureType::Sew||f.type==FeatureType::SurfaceEdit||f.type==FeatureType::ConstructionFrame||f.type==FeatureType::PCB||f.type==FeatureType::CableRoute||f.type==FeatureType::Suspension)
            v.set("native",f.nativeSpec.t==Value::Obj?f.nativeSpec:Value::makeObj());
        if(isCurveFeature(f.type)||f.type==FeatureType::MeshEdit)v.set("native",f.nativeSpec.t==Value::Obj?f.nativeSpec:Value::makeObj());
        if(f.type==FeatureType::MeshBody)v.set("mesh",f.sourceData);
        fs.arr.push_back(std::move(v));
    }
    r.set("features",std::move(fs));
    if(extended){
        r.set("next_assembly_id",Value::makeNum((double)doc.nextAssemblyId_));Value nodes=Value::makeArr();
        for(auto& n:doc.assembly){Value j=Value::makeObj(),p=Value::makeObj();j.set("id",Value::makeNum((double)n.id));j.set("parent",Value::makeNum((double)n.parent));j.set("feature",Value::makeNum((double)n.feature));j.set("name",Value::makeStr(n.name));j.set("visible",Value::makeBool(n.visible));j.set("fixed",Value::makeBool(n.fixed));
            std::vector<std::string> keys;for(auto& kv:n.p)keys.push_back(kv.first);std::sort(keys.begin(),keys.end());for(auto& k:keys)p.set(k,Value::makeStr(n.p.at(k)));j.set("p",p);
            if(n.pattern.active)j.set("pattern",assemblyPatternToJson(n.pattern));
            nodes.arr.push_back(j);}
        r.set("assembly",nodes);
    }
    if(!doc.mates.empty()||doc.nextMateId_>1){Value mates=Value::makeArr();for(auto& m:doc.mates)mates.arr.push_back(mateToJson(m));r.set("mates",mates);r.set("next_mate_id",Value::makeNum((double)doc.nextMateId_));}
    if(includeAudit&&doc.agentActivity){r.set("agent_activity",agentActivityJson(doc.agentActivity));r.set("version",Value::makeNum(33));}
    if(generation34Document(doc))r.set("version",Value::makeNum(34));
    if(skeletonDocument(doc))r.set("skeleton",doc.skeleton);
    if(generation35Document(doc))r.set("version",Value::makeNum(35));
    if(!doc.drawings.empty()){Value drawings=Value::makeArr();for(const auto& drawing:doc.drawings)drawings.arr.push_back(drawing);r.set("drawings",drawings);}
    if(generation36Document(doc)||!doc.drawings.empty())r.set("version",Value::makeNum(36));
    if(generation37Document(doc))r.set("version",Value::makeNum(37));
    if(doc.designContainerCheckout.t!=Value::Null)r.set("design_container_checkout",doc.designContainerCheckout);
    if(generation38Document(doc))r.set("version",Value::makeNum(38));
    if(appearance024BNeedsVersion39(doc))r.set("version",Value::makeNum(39));
    if(doc.historyTip||!doc.assemblyConfigurations.isNull()||!doc.assemblyLoadSelection.isNull()||!doc.assemblyExternalReferences.isNull()){
        r.set("version",Value::makeNum(40));
        if(doc.historyTip)r.set("history_tip",Value::makeNum((double)doc.historyTip));
        if(!doc.assemblyConfigurations.isNull())r.set("assembly_configurations",doc.assemblyConfigurations);
        if(!doc.assemblyLoadSelection.isNull())r.set("assembly_load_selection",doc.assemblyLoadSelection);
        if(!doc.assemblyExternalReferences.isNull())r.set("assembly_external_references",doc.assemblyExternalReferences);
    }
    if(generation41Document(doc))r.set("version",Value::makeNum(41));
    if(generation42Document(doc))r.set("version",Value::makeNum(42));
    if(modeling031BNeedsVersion43(doc))r.set("version",Value::makeNum(43));
    for(const auto& node:doc.assembly)if(assemblyPatternHasCarry(node.pattern)){r.set("version",Value::makeNum(44));break;}
    for(const auto& feature:doc.features)if(requiresElectronics035B(feature)){r.set("version",Value::makeNum(45));break;}
    for(const auto& plane:doc.planes)if(plane.support.t==Value::Obj)if(const auto* kind=plane.support.find("kind");kind&&kind->t==Value::Str&&kind->str=="bisector"){r.set("version",Value::makeNum(46));break;}
    if(motionsRequireDocumentVersion47(doc))r.set("motions",doc.motions);
    if(curvePattern044BNeedsVersion47(doc)||media044BNeedsVersion47(doc)||motionsRequireDocumentVersion47(doc))r.set("version",Value::makeNum(47));
    if(includeAudit&&includeHistory)iterationPersist(r,doc);
    canonicalDocumentKeys(r);
    std::string serialized=r.dump();
    if(serialized.size()>maxDocumentBytes)throw std::runtime_error("Document exceeds 4 MiB logical capacity, including embedded images and retained history");
    return serialized;
}

static bool documentFromValue(Value& v,Document& doc,std::string* err){
    auto fail=[&](const std::string& m){ if(err)*err=m; return false; };
    if(v.t!=Value::Obj) return fail("root must be object");
    if(auto ver=v.find("version"); ver && (ver->t!=Value::Num || !std::isfinite(ver->num)||ver->num!=std::floor(ver->num)||ver->num<1||ver->num>47))
        return fail("unsupported document version");
    Value history;std::string historyError;if(!iterationExpand(v,history,historyError))return fail(historyError);
    if(const auto* nodes=v.find("assembly");nodes&&nodes->t==Value::Arr)for(const auto& node:nodes->arr)if(const auto* pattern=node.find("pattern");pattern&&pattern->find("carry_frame")){
        const auto* version=v.find("version");if(!version||version->num<44)return fail("Pattern carry frames require document version 44");
    }

    if(auto format=v.find("format");format && format->asStr()!="ark")return fail("unsupported format");
    if(auto units=v.find("units");units && units->asStr()!="mm")return fail("unsupported units (expected mm)");
    if(!v.find("features"))return fail("missing features array");
    for(auto key:{"params","sketches","features","planes","canvases","materials","image_assets","stamps"})if(auto a=v.find(key);a && a->t!=Value::Arr)return fail(std::string(key)+" must be an array");
    Document nd;   // parse into scratch; commit only on full success
    try{
        if(const auto* assets=v.find("image_assets")){if(assets->arr.size()>64)return fail("Embedded image asset capacity is 64");for(const auto& asset:assets->arr)nd.imageAssets.push_back(imageAssetFromJson(asset));}
        if(const auto* stamps=v.find("stamps")){if(stamps->arr.size()>128)return fail("Stamp capacity is 128");for(const auto& stamp:stamps->arr)nd.stamps.push_back(stampFromJson(stamp));}
    }catch(const std::exception& error){return fail(error.what());}
    if(const auto* checkout=v.find("design_container_checkout")){const auto* version=v.find("version");std::string reason;if(!version||version->num<38||!validateDesignContainerManifest(*checkout,reason))return fail(reason.empty()?"Design container workspace requires version 38":reason);nd.designContainerCheckout=*checkout;}
    if(const auto* drawings=v.find("drawings")){
        const auto* version=v.find("version");if(!version||version->num<36||drawings->t!=Value::Arr||drawings->arr.size()>256)return fail("Technical drawings require document version 36 and at most 256 authored drawings");
        std::set<std::string> names;try{for(const auto& drawing:drawings->arr){validateDrawing(drawing,true);if(!names.insert(drawing.find("name")->str).second)return fail("Duplicate technical drawing name");nd.drawings.push_back(drawing);}}catch(const std::exception& error){return fail(error.what());}
    }
    if(const auto* skeleton=v.find("skeleton")){
        const auto* version=v.find("version");if(!version||version->num<35||skeleton->t!=Value::Obj)return fail("Skeleton source requires document version 35");
        const auto* language=skeleton->find("language"),*source=skeleton->find("source"),*overrides=skeleton->find("overrides"),*manifest=skeleton->find("manifest");
        if(!language||language->t!=Value::Str||language->str!="ark.skeleton.v1"||!source||source->t!=Value::Str||source->str.size()>65536||!overrides||overrides->t!=Value::Obj||!manifest||manifest->t!=Value::Obj)return fail("Invalid skeleton source/ownership object");
        for(const auto& entry:skeleton->obj)if(entry.first!="language"&&entry.first!="source"&&entry.first!="overrides"&&entry.first!="manifest")return fail("Unknown skeleton field");
        for(const auto& entry:overrides->obj)if(entry.second.t!=Value::Str||entry.second.str.size()>2048)return fail("Invalid skeleton input override");
        for(const char* key:{"parameters","arrays","features"}){const auto* rows=manifest->find(key);if(!rows||rows->t!=Value::Arr||rows->arr.size()>2048)return fail("Invalid skeleton ownership rows");for(const auto& row:rows->arr)if(row.t!=Value::Obj||!row.find("name")||row.find("name")->t!=Value::Str)return fail("Invalid skeleton ownership row");}
        nd.skeleton=*skeleton;
    }
    if(const auto* bom=v.find("bom_document_id")){const auto* version=v.find("version");if(!version||version->num<32||bom->t!=Value::Str||!validBomDocumentId(bom->str))return fail("BOM identity requires a valid document version 32 identity");nd.bomDocumentId=bom->str;}
    if(const auto* activity=v.find("agent_activity")){const auto* version=v.find("version");if(!version||version->num<33)return fail("Agent activity requires document version 33");try{nd.agentActivity=readAgentActivity(*activity);}catch(const std::exception& error){return fail(error.what());}}
    if(const Value* usage=v.find("usage")){
        const Value* version=v.find("version");if(!version||version->num<17)return fail("Usage ledger requires document version 17");
        if(!usageLedgerFromJson(*usage,nd.usage,err))return false;
    }
    std::set<std::string> paramNames,sketchNames;
    std::set<u64> featureIds;
    if(auto ps=v.find("params"))
        for(auto&p:ps->arr){
            Parameter np;
            np.name=p.find("name")?p.find("name")->asStr():"";
            np.expr=p.find("expr")?p.find("expr")->asStr():"0";
            if(np.name.empty() || !paramNames.insert(np.name).second) return fail("empty or duplicate parameter name");
            nd.params.push_back(std::move(np));
        }

    if(auto materials=v.find("materials")){
        if(materials->arr.size()>256)return fail("Material capacity exceeded");
        std::set<std::string> names;
        for(const auto& item:materials->arr){
            if(item.t!=Value::Obj)return fail("Material must be an object");
            Material material;auto name=item.find("name");if(!name||name->t!=Value::Str)return fail("Material needs a name");material.name=name->str;
            if(!names.insert(material.name).second)return fail("Duplicate material name");
            if(auto physical=item.find("physical")){auto version=v.find("version");if(!version||version->asNum()<20)return fail("Physical material properties require version 20");material.physical=*physical;}
            if(auto density=item.find("density")){if(density->t!=Value::Num)return fail("Material density must be numeric");material.densityGcm3=density->num;}
            if(auto appearance=item.find("appearance")){std::string reason;if(!applyAppearancePatch(material.appearance,*appearance,reason))return fail(reason);}
            std::string reason;if(!validateMaterial(material,reason))return fail(reason);nd.materials.push_back(std::move(material));
        }
    }

    if(auto sks=v.find("sketches"))
        for(auto&s:sks->arr){
            NamedSketch ns;
            ns.name=s.find("name")?s.find("name")->asStr():"";
            if(ns.name.empty() || !sketchNames.insert(ns.name).second) return fail("empty or duplicate sketch name");
            if(!sketchFromJson(s.dump(),ns.sk,err))return false;
            nd.sketches.push_back(std::move(ns));
        }

    if(auto planes=v.find("planes")){
        if(planes->arr.size()>128)return fail("Plane capacity exceeded");
        std::set<std::string> names{"XY","XZ","YZ"};
        for(auto& j:planes->arr){DatumPlane plane;
            auto name=j.find("name");if(!name||name->t!=Value::Str||name->str.empty()||name->str.size()>128||!names.insert(name->str).second)return fail("Invalid or duplicate plane name");plane.name=name->str;
            if(auto parent=j.find("parent")){if(parent->t!=Value::Str||parent->str.empty())return fail("Plane parent must be a name");plane.parent=parent->str;}
            if(auto p=j.find("p")){if(p->t!=Value::Obj)return fail("Plane p must be object");for(auto& kv:p->obj){if(kv.second.t!=Value::Str)return fail("Plane values must be expressions");plane.p[kv.first]=kv.second.str;}}
            if(auto kind=j.find("kind")){if(kind->t!=Value::Str||(kind->str!="plane"&&kind->str!="axis"&&kind->str!="frame"&&kind->str!="point"))return fail("Invalid datum kind");plane.kind=kind->str;}
            if(auto support=j.find("support")){if(support->t!=Value::Obj)return fail("Datum support must be object");plane.support=*support;}
            nd.planes.push_back(std::move(plane));
        }
    }
    for(const auto& plane:nd.planes)if(plane.support.t==Value::Obj)if(const auto* kind=plane.support.find("kind");kind&&kind->t==Value::Str&&kind->str=="bisector"){
        const auto* version=v.find("version");if(!version||version->num<46)return fail("Bisector planes require document version 46");
    }
    if(auto canvases=v.find("canvases")){
        if(canvases->arr.size()>8)return fail("Canvas capacity exceeded");
        std::set<std::string> names;
        for(auto& j:canvases->arr){
            if(j.t!=Value::Obj)return fail("Canvas must be an object");
            PlaneCanvas canvas;
            auto name=j.find("name");
            if(!name||name->t!=Value::Str||name->str.empty()||name->str.size()>128||!names.insert(name->str).second)return fail("Canvas name must be 1..128 characters");
            canvas.name=name->str;
            if(auto plane=j.find("plane")){if(plane->t!=Value::Str||plane->str.empty()||plane->str.size()>128)return fail("Canvas plane must be a name");canvas.plane=plane->str;}
            if(auto asset=j.find("asset")){if(asset->t!=Value::Str||asset->str.empty()||asset->str.size()>128)return fail("Canvas asset must name an embedded image");canvas.asset=asset->str;}
            auto path=j.find("path");
            if(path){if(path->t!=Value::Str||path->str.size()>1024||(path->str.empty()&&canvas.asset.empty()))return fail("Canvas path must be 1..1024 characters or an embedded asset must be specified");canvas.path=path->str;}
            else if(canvas.asset.empty())return fail("Canvas needs a legacy path or embedded asset");
            auto takeExpr=[&](const char* key,std::string& out){auto value=j.find(key);if(!value)return fail(std::string("Canvas needs ")+key);if(value->t==Value::Str&&!value->str.empty()&&value->str.size()<=2048){out=value->str;return true;}if(value->t==Value::Num&&std::isfinite(value->num)){out=value->dump();return true;}return fail("Canvas width and height must be expressions");};
            if(!takeExpr("width",canvas.width)||!takeExpr("height",canvas.height))return false;
            if(auto opacity=j.find("opacity")){if(opacity->t!=Value::Num||!std::isfinite(opacity->num)||opacity->num<0||opacity->num>1)return fail("Canvas opacity must be 0..1");canvas.opacity=opacity->num;}
            nd.canvases.push_back(std::move(canvas));
        }
    }

    u64 maxId=0;
    if(auto fs=v.find("features"))
        for(auto&fv:fs->arr){
            Feature nf;
            auto identifier=fv.find("id");
            if(!identifier||identifier->t!=Value::Num||!std::isfinite(identifier->num)||identifier->num<1||identifier->num>9007199254740990.||identifier->num!=std::floor(identifier->num))return fail("invalid feature ID");
            nf.id=(u64)identifier->num;
            if(!featureIds.insert(nf.id).second)return fail("duplicate feature ID");
            std::string tn=fv.find("type")?fv.find("type")->asStr():"";
            if(!featureTypeFromName(tn,nf.type)) return fail("unknown feature type: "+tn);
            nf.name=fv.find("name")?fv.find("name")->asStr():"";
            if(const auto* part=fv.find("part")){if(nd.bomDocumentId.empty())return fail("Part metadata requires persistent BOM identity and version 32");try{nf.part=validatePartMetadata(*part);}catch(const std::exception& error){return fail(error.what());}}
            if(auto manufacturing=fv.find("manufacturing")){
                auto route=manufacturing->find("route"),version=v.find("version");
                if(manufacturing->t!=Value::Obj||!route||route->t!=Value::Str||!version||version->asNum()<21)return fail("Manufacturing route requires version 21 object");
                if((route->str=="injection_molding"||route->str=="additive"||manufacturing->find("process"))&&version->asNum()<23)return fail("Route-specific manufacturing process requires version 23");
                std::string reason;if(!applyManufacturingIntent(nf,*manufacturing,reason))return fail(reason);
            }
            nf.opInt=(int)(fv.find("op")?fv.find("op")->asNum():0);
            if(auto flag=fv.find("suppressed")){if(flag->t!=Value::Bool)return fail("suppressed must be boolean");nf.suppressed=flag->b;}
            if(auto material=fv.find("material")){if(material->t!=Value::Str||material->str.empty())return fail("Feature material must be a name");nf.material=material->str;}
            if(auto appearance=fv.find("appearance")){Appearance tested;std::string reason;if(!applyAppearancePatch(tested,*appearance,reason))return fail(reason);nf.appearance=*appearance;}
            if(auto faces=fv.find("face_appearances")){if(faces->t!=Value::Arr||faces->arr.size()>128)return fail("Face appearances must be an array of at most 128 entries");try{for(const auto& appearance:faces->arr)nf.faceAppearances.push_back(faceAppearanceFromJson(appearance));}catch(const std::exception& error){return fail(error.what());}}
            if(auto in=fv.find("inputs")){
                if(in->t!=Value::Arr)return fail("inputs must be an array");
                for(auto&idv:in->arr){
                    if(idv.t!=Value::Num || idv.num<1 || idv.num>9007199254740990. || idv.num!=std::floor(idv.num))return fail("invalid input ID");
                    u64 iid=(u64)idv.num;
                    if(iid==nf.id||!featureIds.count(iid))return fail("input must reference an earlier feature");
                    nf.inputs.push_back(iid);
                }
            }
            if(auto pv=fv.find("p"); pv&&pv->t==Value::Obj)
                for(auto&kv:pv->obj) nf.p[kv.first]=kv.second.asStr();
            if(auto bends=fv.find("bends")){
                if(bends->t!=Value::Arr||bends->arr.size()>64)return fail("invalid tube bends");
                for(auto& j:bends->arr){if(j.t!=Value::Obj)return fail("bend must be object");TubeBend b;for(auto pair:std::vector<std::pair<const char*,std::string*>>{{"feed",&b.feed},{"radius",&b.radius},{"angle",&b.angle},{"roll",&b.roll}})if(auto value=j.find(pair.first)){if(value->t!=Value::Str)return fail("tube bend fields must be expressions");*pair.second=value->str;}nf.bends.push_back(b);}
            }
            if(nf.type==FeatureType::NotchedTube){
                auto version=v.find("version");if(!version||version->asNum()<3)return fail("notched_tube requires document version 3");
                auto points=fv.find("notches");if(!points||points->t!=Value::Arr||points->arr.size()>64)return fail("invalid notch points array");
                for(auto& point:points->arr){auto station=point.find("station"),angle=point.find("angle"),face=point.find("face");if(!station||!angle||!face||station->t!=Value::Str||angle->t!=Value::Str||face->t!=Value::Str)return fail("invalid notch point fields");nf.notches.push_back({station->str,angle->str,face->str});}
            }
            if(nf.type==FeatureType::SourceTube){auto version=v.find("version"),data=fv.find("source");if(!version||version->asNum()<4||!data||data->t!=Value::Obj)return fail("source_tube requires version 4 package");nf.sourceData=*data;}
            if(nf.type==FeatureType::SourceSheet){auto version=v.find("version"),data=fv.find("source");if(!version||version->asNum()<6||!data||data->t!=Value::Obj)return fail("source_sheet requires version 6 package");nf.sourceData=*data;}
            if(nf.type==FeatureType::Loft||nf.type==FeatureType::SurfaceFill||nf.type==FeatureType::BSplineSurface||nf.type==FeatureType::Thicken||nf.type==FeatureType::SheetMetal||nf.type==FeatureType::NativeSolid){auto version=v.find("version"),spec=fv.find("native");if(!version||version->asNum()<6||!spec||spec->t!=Value::Obj)return fail("native feature requires version 6 native object");nf.nativeSpec=*spec;}
            if(nf.type==FeatureType::NativeSolid&&featureOperation(nf)=="imported_cad"){std::string reason;if(!validateCadPackageAuthoring(nf,reason))return fail(reason);}
            if(nf.type==FeatureType::NativeSolid&&featureOperation(nf)=="imported_brep"){auto version=v.find("version");if(!version||version->asNum()<24)return fail("Source-owned library component requires document version 24");std::string reason;if(!validateImportedComponentDefinition(nf,reason))return fail(reason);}
            if(nf.type==FeatureType::SheetMetal)for(const char* key:{"hems","stamps"})if(auto rows=nf.nativeSpec.find(key)){auto version=v.find("version");if(rows->t!=Value::Arr)return fail("Sheet hems and stamps must be arrays");if(!rows->arr.empty()&&(!version||version->asNum()<21))return fail("Sheet hems and stamps require document version 21");}
            if(generation17SheetOpening(nf)){auto version=v.find("version");if(!version||version->asNum()<17)return fail("authored sheet slots or windows require document version 17");}
            if(nf.type==FeatureType::SurfaceEdit){auto version=v.find("version"),spec=fv.find("native");if(!version||version->asNum()<26||!spec||spec->t!=Value::Obj)return fail("surface_edit requires version 26 native object");nf.nativeSpec=*spec;}
            if(nf.type==FeatureType::Sweep||nf.type==FeatureType::ExtrudeSurface||nf.type==FeatureType::RevolveSurface||nf.type==FeatureType::OffsetSurface||nf.type==FeatureType::Sew){auto version=v.find("version"),spec=fv.find("native");if(!version||version->asNum()<8||!spec||spec->t!=Value::Obj)return fail("extended surface feature requires version 8 native object");nf.nativeSpec=*spec;}
            if(isMeshModelFeature(nf.type)){auto version=v.find("version");if(!version||version->asNum()<8)return fail("mesh modeling features require document version 8");}
            if(nf.type==FeatureType::ConstructionFrame||nf.type==FeatureType::PCB||nf.type==FeatureType::CableRoute){auto version=v.find("version"),spec=fv.find("native");if(!version||version->asNum()<16||!spec||spec->t!=Value::Obj)return fail("Engineering modules require version 16 native object");nf.nativeSpec=*spec;}
            if(nf.type==FeatureType::Suspension){auto version=v.find("version"),spec=fv.find("native");if(!version||version->asNum()<18||!spec||spec->t!=Value::Obj)return fail("Suspension requires version 18 native program");nf.nativeSpec=*spec;}
            if(isCurveFeature(nf.type)){auto version=v.find("version"),spec=fv.find("native");if(!version||version->asNum()<12||!spec||spec->t!=Value::Obj)return fail("points, curve segments, or curve features require document version 12");nf.nativeSpec=*spec;}
            if(generation9Feature(nd,nf)){auto version=v.find("version");if(!version||version->asNum()<9)return fail("draft, pattern, mirror, edge selection, loft smoothing, or a point tip requires document version 9");}
            if(nf.type==FeatureType::MeshBody){auto mesh=fv.find("mesh");if(!mesh||mesh->t!=Value::Obj)return fail("mesh_body requires a mesh object");nf.sourceData=*mesh;}
            if(nf.type==FeatureType::MeshEdit){const auto* version=v.find("version"),*spec=fv.find("native");if(!version||version->num<35||!spec||spec->t!=Value::Obj)return fail("mesh_edit requires document version 35 and native selection recipe");nf.nativeSpec=*spec;}
            maxId=tmax(maxId,nf.id);
            nd.features.push_back(std::move(nf));
        }
    nd.nextFeatureId_=maxId+1;
    if(auto next=v.find("next_feature_id")){
        if(next->t!=Value::Num||next->num<(double)(maxId+1)||next->num>9007199254740991.||next->num!=std::floor(next->num))return fail("invalid next_feature_id");
        nd.nextFeatureId_=(u64)next->num;
    }

    if(auto nodes=v.find("assembly")){
        if(nodes->t!=Value::Arr||nodes->arr.size()>4096)return fail("invalid assembly array");
        auto integer=[&](const Value& j,const char* key,u64& out){auto q=j.find(key);if(!q||q->t!=Value::Num||q->num<0||q->num>9007199254740990.||q->num!=std::floor(q->num))return false;out=(u64)q->num;return true;};
        u64 maximum=0;for(auto& j:nodes->arr){AssemblyNode n;
            if(!integer(j,"id",n.id)||!n.id||!integer(j,"parent",n.parent)||!integer(j,"feature",n.feature))return fail("invalid assembly identifiers");
            n.name=j.find("name")?j.find("name")->asStr():"Component";
            if(auto visible=j.find("visible")){if(visible->t!=Value::Bool)return fail("visible must be boolean");n.visible=visible->b;}
            if(auto p=j.find("p")){if(p->t!=Value::Obj)return fail("assembly p must be object");for(auto& kv:p->obj){if(kv.second.t!=Value::Str)return fail("placement fields must be expressions");n.p[kv.first]=kv.second.str;}}
            if(auto fixed=j.find("fixed")){if(fixed->t!=Value::Bool)return fail("fixed must be boolean");n.fixed=fixed->b;}
            if(auto pattern=j.find("pattern")){
                try{readAssemblyPattern(n.pattern,*pattern);}catch(const std::exception& error){return fail(error.what());}
            }
            maximum=std::max(maximum,n.id);nd.assembly.push_back(n);
        }
        nd.nextAssemblyId_=maximum+1;
        if(v.find("next_assembly_id")){u64 counter;if(!integer(v,"next_assembly_id",counter)||counter<=maximum)return fail("invalid next_assembly_id");nd.nextAssemblyId_=counter;}
    }
    if(auto list=v.find("mates")){
        if(list->t!=Value::Arr||list->arr.size()>256)return fail("Invalid mates array");
        std::set<u64> seen;try{for(auto& j:list->arr){auto key=j.find("id");if(!key||key->t!=Value::Num||key->num<1||key->num>9007199254740990.||key->num!=std::floor(key->num))return fail("Invalid mate id");AssemblyMate m;m.id=(u64)key->num;if(!seen.insert(m.id).second)return fail("Duplicate mate id");editMate(m,j);if(m.type=="prismatic"||m.limits.enabled){auto version=v.find("version");if(!version||version->asNum()<31)return fail("Prismatic mates and joint limits require document version 31");}if(m.type=="rigid"||m.type=="revolute"){auto version=v.find("version");if(!version||version->asNum()<29)return fail("Rigid and revolute mates require document version 29");}if(isMechanicalMate(m.type)){auto version=v.find("version");if(!version||version->asNum()<22)return fail("Mechanical couplings require document version 22");}nd.nextMateId_=std::max(nd.nextMateId_,m.id+1);nd.mates.push_back(m);}}
        catch(const std::exception& e){return fail(e.what());}
    }
    if(auto key=v.find("next_mate_id")){if(key->t!=Value::Num||key->num<(double)nd.nextMateId_||key->num>9007199254740991.||key->num!=std::floor(key->num))return fail("Invalid next mate id");nd.nextMateId_=(u64)key->num;}
    for(const auto& f:nd.features)if(f.type==FeatureType::NativeSolid){auto op=f.nativeSpec.find("op");if((op&&(op->str=="curve_pattern"||op->str=="hole_pattern"))||f.get("op")=="curve_pattern"||f.get("op")=="hole_pattern"){auto version=v.find("version");if(!version||version->asNum()<19)return fail("Curve patterns require document version 19");}}
    if(generation10Document(nd)){auto version=v.find("version");if(!version||version->asNum()<10)return fail("placed-instance booleans, unions of more than two exact solids, or instance patterns require document version 10");}
    if(generation11Document(nd)){auto version=v.find("version");if(!version||version->asNum()<11)return fail("solid shells or loft guide curves require document version 11");}
    if(generation12Document(nd)){auto version=v.find("version");if(!version||version->asNum()<12)return fail("points, curve segments, or curve features require document version 12");}
    if(generation13Document(nd)){auto version=v.find("version");if(!version||version->asNum()<13)return fail("plane canvases, suppressed features, or projected and offset sketch entities require document version 13");}
    bool projectionVersion=false;
    for(const auto& sketch:nd.sketches)for(const auto& entity:sketch.sk.ents)if(entity.kind==SketchEnt::Project&&(entity.projectionMode!="legacy"||entity.sourceInstance||!entity.sourceSketch.empty()))projectionVersion=true;
    for(const auto& feature:nd.features)if(feature.type==FeatureType::Curve)if(auto op=feature.nativeSpec.find("op");op&&op->str=="project")projectionVersion=true;
    if(projectionVersion){auto version=v.find("version");if(!version||version->asNum()<28)return fail("Exact normal projection and spatial included geometry require document version 28");}
    if(generation14Document(nd)){auto version=v.find("version");if(!version||version->asNum()<14)return fail("sketch fillets, sketch chamfers, or mates across assembly groups require document version 14");}
    if(!nd.materials.empty()){auto version=v.find("version");if(!version||version->asNum()<15)return fail("Materials require document version 15");}
    for(const auto& feature:nd.features){
        if(generation15NativeFeature(feature)){auto version=v.find("version");if(!version||version->asNum()<15)return fail("split_plane or mirror copy_only requires document version 15");}
        if(!feature.material.empty()){
            auto version=v.find("version");if(!version||version->asNum()<15)return fail("Feature appearance requires document version 15");
            if(std::none_of(nd.materials.begin(),nd.materials.end(),[&](const Material& material){return material.name==feature.material;}))return fail("Feature references a missing material: "+feature.material);
        }
        if(feature.appearance.t==Value::Obj){auto version=v.find("version");if(!version||version->asNum()<15)return fail("Feature appearance requires document version 15");}
    }
    if(nd.params.size()>256||nd.features.size()>1024||nd.sketches.size()>64||nd.canvases.size()>8||nd.materials.size()>256)return fail("document capacity exceeded");
    nd.refreshParams();
    if(!nd.parameterError.empty())return fail(nd.parameterError);
    for(auto& canvas:nd.canvases){
        bool known=canvas.plane=="XY"||canvas.plane=="XZ"||canvas.plane=="YZ";
        for(auto& plane:nd.planes) if(plane.name==canvas.plane) known=true;
        if(!known) return fail("Canvas plane was not found");
        auto lengthOk=[&](const std::string& expr){bool ok=false;double value=evalExpr(expr,nd.buildEnv(),&ok);return ok&&value>0.&&value<=1e6;};
        if(!lengthOk(canvas.width)||!lengthOk(canvas.height)) return fail("Canvas width and height must be positive millimetres");
    }
    std::vector<AssemblyPlacement> placements;std::string assemblyError;
    const bool sourcePatterns=std::any_of(nd.assembly.begin(),nd.assembly.end(),[](const AssemblyNode& n){return specializedAssemblyPattern(n.pattern);});
    if(sourcePatterns){if(!validateAssemblyPatternAuthoring(nd,assemblyError))return fail(assemblyError);}
    else if(nd.mates.empty()&&!resolveAssembly(nd,placements,assemblyError))return fail(assemblyError);
    bool supported=false;for(auto& p:nd.planes)supported|=p.support.t==Value::Obj;
    try{if(!supported){for(auto& plane:nd.planes)resolvePlane(nd,plane.name);for(auto& sketch:nd.sketches)resolvePlane(nd,sketch.sk.plane);for(auto& canvas:nd.canvases)resolvePlane(nd,canvas.plane);}}
    catch(const std::exception& e){return fail(e.what());}
    for(const auto& feature:nd.features){bool fabrication=false;if(feature.type==FeatureType::SheetMetal)fabrication=feature.nativeSpec.find("jog")||feature.nativeSpec.find("sketch_bend");if(feature.type==FeatureType::ConstructionFrame){if(auto welds=feature.nativeSpec.find("welds"))fabrication|=welds->t==Value::Arr&&!welds->arr.empty();if(auto joints=feature.nativeSpec.find("joints"))for(const auto& joint:joints->arr)if(auto kind=joint.find("kind"))fabrication|=kind->str=="trim_extend"||kind->str=="trim_plane";}if(fabrication&&(!v.find("version")||v.find("version")->num<25))return fail("Frame endpoint/weld or sheet jog/sketch-bend programs require document version 25");}
    for(const auto& field:{std::pair{"assembly_configurations",&nd.assemblyConfigurations},std::pair{"assembly_load_selection",&nd.assemblyLoadSelection},std::pair{"assembly_external_references",&nd.assemblyExternalReferences}})if(const auto* value=v.find(field.first)){
        const auto* version=v.find("version");if(!version||version->num<40||value->t!=Value::Arr)return fail(std::string(field.first)+" requires document version 40 array");*field.second=*value;
    }
    if(const auto* motions=v.find("motions")){
        const auto* version=v.find("version");if(!version||version->num<47||motions->t!=Value::Arr)return fail("Baked motions require document version 47 array");
        try{validateMotionStorage(*motions);}catch(const std::exception& error){return fail(error.what());}nd.motions=*motions;
    }
    if(const auto* tip=v.find("history_tip")){
        const auto* version=v.find("version");if(!version||version->num<40||tip->t!=Value::Num||!std::isfinite(tip->num)||tip->num<1||tip->num>9007199254740990.||tip->num!=std::floor(tip->num)||!nd.find((u64)tip->num))return fail("history_tip requires a valid feature ID and document version 40");nd.historyTip=(u64)tip->num;
    }
    try{validateAssemblyAuthoring(nd);}catch(const std::exception& error){return fail(error.what());}
    std::string manufacturingError;if(!validateManufacturingRoutes(nd,manufacturingError))return fail(manufacturingError);
    if(history.t!=Value::Null){if(!iterationValidate(nd,history,historyError))return fail(historyError);nd.iterationHistory=std::move(history);}
    if(arcRecipeDocument(nd)){const auto* version=v.find("version");if(!version||version->num<30)return fail("Arc recipes and linked tube end fits require document version 30");}
    if(generation34Document(nd)){const auto* version=v.find("version");if(!version||version->num<34)return fail("Typed sketches, associative datums, spatial constraints and varying sections require document version 34");}
    if(generation35Document(nd)){const auto* version=v.find("version");if(!version||version->num<35)return fail("Skeleton, indexed mesh edits, current-B-rep references and extended native controls require document version 35");}
    if(generation36Document(nd)){const auto* version=v.find("version");if(!version||version->num<36)return fail("Reverse, symmetric, face-source and face-bounded extrusion require document version 36");}
    if(generation37Document(nd)){const auto* version=v.find("version");if(!version||version->num<37)return fail("Specialized component patterns, advanced sheet programs and drawing source/template recipes require document version 37");}
    if(generation38Document(nd)){const auto* version=v.find("version");if(!version||version->num<38)return fail("Exact CAD imports, Holes Orchestrator and isolated containers require document version 38");}
    if(appearance024BNeedsVersion39(nd)){const auto* version=v.find("version");if(!version||version->num<39)return fail("Embedded images, stamps, face appearances and exact split lines require document version 39");}
    if(generation41Document(nd)){const auto* version=v.find("version");if(!version||version->num<41)return fail("Scoped container v2 and explicit surface orientation repair require document version 41");}
    if(generation42Document(nd)){const auto* version=v.find("version");if(!version||version->num<42)return fail("Emission, authored nets and exact wood/vent recipes require document version 42");}
    if(modeling031BNeedsVersion43(nd)){const auto* version=v.find("version");if(!version||version->num<43)return fail("031B advanced modeling and occurrence-bound cable recipes require document version 43");}
    for(const auto& feature:nd.features)if(requiresElectronics035B(feature)){const auto* version=v.find("version");if(!version||version->num<45)return fail("Smooth/ribbon cable recipes and endpoint electrical metadata require document version 45");}
    for(const auto& node:nd.assembly)if(assemblyPatternHasCarry(node.pattern)){const auto* version=v.find("version");if(!version||version->num<44)return fail("Pattern carry frames require document version 44");}
    if(curvePattern044BNeedsVersion47(nd)){const auto* version=v.find("version");if(!version||version->num<47)return fail("044B curve pattern paths, pitch spacing, frames and support inputs require document version 47");}
    if(media044BNeedsVersion47(nd)){const auto* version=v.find("version");if(!version||version->num<47)return fail("044B procedural appearance patterns, animated GIF/video assets and stamp playback require document version 47");}
    try{validateAppearanceAuthoring024B(nd,false);}catch(const std::exception& error){return fail(error.what());}
    try{validateDesignContainerWorkspace(nd);validateSkeletonAuthoring(nd);}catch(const std::exception& error){return fail(error.what());}
    doc=std::move(nd);
    return true;
}

bool documentFromJson(const std::string& json,Document& doc,std::string* err){
    if(json.size()>maxDocumentBytes){if(err)*err="document exceeds 4 MiB";return false;}
    Value root;if(!arkjson::parse(json,root)){if(err)*err="malformed JSON";return false;}
    return documentFromValue(root,doc,err);
}

// ---------------------------------------------------------------------------
std::string sketchToJson(const Sketch& sk){
    Value v=Value::makeObj();
    v.set("plane",Value::makeStr(sk.plane));v.set("tolerance",Value::makeNum(sk.tolerance));
    Value es=Value::makeArr(); for(auto&e:sk.ents) es.arr.push_back(entToJson(e));
    Value cs=Value::makeArr(); for(auto&c:sk.cons) cs.arr.push_back(conToJson(c));
    v.set("entities",std::move(es));
    v.set("constraints",std::move(cs));
    if(!sk.corners.empty()){
        Value corners=Value::makeArr();
        for(const auto& corner:sk.corners){
            Value item=Value::makeObj();
            item.set("entity",Value::makeStr(corner.entity));
            item.set("corner",Value::makeNum(corner.corner));
            item.set("kind",Value::makeStr(corner.chamfer?"chamfer":"fillet"));
            item.set("size",Value::makeStr(corner.size));
            corners.arr.push_back(std::move(item));
        }
        v.set("corners",std::move(corners));
    }
    return v.dump();
}

bool sketchFromJson(const std::string& json, Sketch& sk, std::string* err){
    Value v;
    if(!arkjson::parse(json,v)){ if(err)*err="malformed JSON"; return false; }
    if(v.t!=Value::Obj){if(err)*err="Sketch must be object";return false;}
    Sketch out;
    if(auto plane=v.find("plane")){if(plane->t!=Value::Str||plane->str.empty()){if(err)*err="Sketch plane must be a name";return false;}out.plane=plane->str;}
    if(auto tolerance=v.find("tolerance")){if(tolerance->t!=Value::Num||!std::isfinite(tolerance->num)||tolerance->num<.0001||tolerance->num>5){if(err)*err="Sketch tolerance must be 0.0001..5 mm";return false;}out.tolerance=tolerance->num;}
    for(auto key:{"entities","constraints"})if(auto list=v.find(key);list&&list->t!=Value::Arr){if(err)*err="Sketch entities/constraints must be arrays";return false;}
    if(auto es=v.find("entities"))
        for(auto&e:es->arr){
            SketchEnt se;
            if(!entFromJson(e,se,err)) return false;
            out.ents.push_back(std::move(se));
        }
    if(auto cs=v.find("constraints"))
        for(auto&c:cs->arr){
            SketchCon sc;
            if(!conFromJson(c,sc,err)) return false;
            out.cons.push_back(sc);
        }
    if(auto corners=v.find("corners")){
        if(corners->t!=Value::Arr){if(err)*err="Sketch corners must be an array";return false;}
        if(corners->arr.size()>64){if(err)*err="Sketch corner capacity exceeded";return false;}
        for(const auto& item:corners->arr){
            SketchCorner corner;
            if(!cornerFromJson(item,corner,err))return false;
            out.corners.push_back(std::move(corner));
        }
    }
    if(!validSketch(out,err))return false;
    sk=std::move(out);
    return true;
}

bool saveDocument(const Document& doc, const char* path, DocumentSaveReport* report){
    if(report)*report={};
    auto fail=[&](std::string reason){if(report)report->error=std::move(reason);return false;};
    if(!path||!*path) return fail("Empty document path");
    if(doc.iterationHistory.t==Value::Obj){std::string historyError;if(!iterationValidate(doc,doc.iterationHistory,historyError))return fail("Iteration history invalid: "+historyError);}
    // Preserve this bool API's failure contract before opening any staging file.
    // Authoring validation and logical-capacity errors are serialization failures.
    std::string j;try{j=documentToJson(doc);}catch(const std::exception& e){return fail(e.what());}
    if(j.size()>maxDocumentBytes)return fail("Document exceeds the total 4 MiB logical capacity");
    Value persistedRoot;if(!arkjson::parse(j,persistedRoot))return fail("Document JSON exceeds the parser nesting limit");
    auto destination=std::filesystem::path(reinterpret_cast<const char8_t*>(path)),temporary=destination;
    auto extension=destination.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!=".arkjson"&&extension!=".json"){
        try{
            NativeDocumentResultsReport selected;const auto derived=nativeDocumentResults(doc,&selected);
            if(report){report->derivedGeometry=true;report->derivedEntries=selected.entries;report->derivedBytes=selected.bytes;report->derivedOmittedEntries=selected.omittedEntries;report->derivedOmittedBytes=selected.omittedBytes;report->derivedCapacityBytes=selected.capacityBytes;}
            std::string error;if(saveDocumentContainer(persistedRoot,path,&error,nullptr,&derived))return true;
            return fail(error.empty()?"ARK container save failed":error);
        }catch(const std::exception& e){return fail(e.what());}
    }
    temporary+=".tmp";
    std::ofstream f(temporary,std::ios::binary|std::ios::trunc);
    if(!f) return fail("Cannot create the staged JSON export");
    f.write(j.data(),(std::streamsize)j.size());f.flush();
    if(!f)return fail("Cannot write the staged JSON export");
    f.close();if(!f)return fail("Cannot close the staged JSON export");
#ifdef _WIN32
    bool committed=MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code ec;std::filesystem::rename(temporary,destination,ec);bool committed=!ec;
#endif
    if(!committed){std::error_code cleanup;std::filesystem::remove(temporary,cleanup);return fail("Cannot replace the JSON export target");}
    return true;
}

bool loadDocument(const char* path, Document& doc, std::string* err){
    if(!path||!*path){ if(err)*err="empty path"; return false; }
    if(documentContainerFile(path)){
        Value root;DocumentDerivedPayloads derived;std::string discarded;
        if(!loadDocumentContainer(path,root,err,&derived,&discarded))return false;
        // The container reader already enforced logical JSON capacity, exact
        // codec limits and checksums. Apply the same authoring validation to
        // its decoded value without constructing and parsing another JSON copy.
        Document candidate;if(!documentFromValue(root,candidate,err))return false;
        try{
            candidate.derivedGeometryStatus=stageNativeDocumentResults(candidate,derived);
            if(!discarded.empty()){
                candidate.derivedGeometryStatus=Value::makeObj();
                candidate.derivedGeometryStatus.set("status",Value::makeStr("discarded"));
                candidate.derivedGeometryStatus.set("reason",Value::makeStr(discarded));
            }
        }catch(const std::exception& failure){
            candidate.derivedGeometryStatus=Value::makeObj();
            candidate.derivedGeometryStatus.set("status",Value::makeStr("discarded"));
            candidate.derivedGeometryStatus.set("reason",Value::makeStr(failure.what()));
        }
        doc=std::move(candidate);return true;
    }
    std::ifstream f(std::filesystem::path(reinterpret_cast<const char8_t*>(path)),std::ios::binary);
    if(!f){ if(err)*err=std::string("cannot open ")+path; return false; }
    f.seekg(0,std::ios::end);auto length=f.tellg();
    if(length<0||length>4*1024*1024){if(err)*err="document exceeds 4 MiB";return false;}f.seekg(0);
    std::string j((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    return documentFromJson(j,doc,err);
}

} // namespace ark
