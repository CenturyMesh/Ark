#include "ark/plane_sections_024b.hpp"
#include "ark/datum_authoring.hpp"
#include "ark/serialize.hpp"
#include "ark/sketch.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
const V& field(const V& object,const char* key){const auto* value=object.find(key);if(!value)throw std::runtime_error(std::string("Missing plane/section field: ")+key);return *value;}
std::string text(const V& object,const char* key){const auto& value=field(object,key);if(value.t!=V::Str||value.str.empty()||value.str.size()>128)throw std::runtime_error(std::string("Plane/section ")+key+" must be a name with 1..128 characters");return value.str;}
void keys(const V& object,const std::set<std::string>& allowed){if(object.t!=V::Obj)throw std::runtime_error("Plane/section request must be an object");for(const auto& item:object.obj)if(!allowed.count(item.first)&&item.first!="expected_revision"&&item.first!="preview")throw std::runtime_error("Unknown plane/section field: "+item.first);}
const NamedSketch* namedSketch(const Document& document,const std::string& name){const NamedSketch* found=nullptr;for(const auto& sketch:document.sketches)if(sketch.name==name){if(found)throw std::runtime_error("Ambiguous sketch name");found=&sketch;}return found;}
void plane(const Document& document,const std::string& name){
    if(name!="XY"&&name!="XZ"&&name!="YZ"){const DatumPlane* found=nullptr;for(const auto& datum:document.planes)if(datum.name==name){if(found)throw std::runtime_error("Ambiguous construction plane");found=&datum;}if(!found||found->kind!="plane"&&found->kind!="frame")throw std::runtime_error("Expected an existing plane/frame datum: "+name);}
    resolvePlane(document,name);
}
void newDatumName(const Document& document,const std::string& name){if(name.empty()||name.size()>128||name=="XY"||name=="XZ"||name=="YZ")throw std::runtime_error("Invalid new construction datum name");for(const auto& datum:document.planes)if(datum.name==name)throw std::runtime_error("Construction datum already exists: "+name);}
void newFeatureName(const Document& document,const std::string& name){for(const auto& feature:document.features)if(feature.name==name)throw std::runtime_error("Feature already exists: "+name);}
V datumCommand(const std::string& name,const char* kind,V support){V result=V::makeObj();result.set("op",V::makeStr("plane.set"));result.set("name",V::makeStr(name));result.set("kind",V::makeStr(kind));result.set("support",support);return result;}
V endpoint(const std::string& sketch,const std::string& entity,const char* end){V result=V::makeObj();result.set("sketch",V::makeStr(sketch));result.set("entity",V::makeStr(entity));result.set("endpoint",V::makeStr(end));return result;}
const Feature& sourceFeature(const Document& document,const V& id){
    const Feature* found=nullptr;for(const auto& feature:document.features)if((id.t==V::Str&&feature.name==id.str)||(id.t==V::Num&&id.num==feature.id)){if(found)throw std::runtime_error("Ambiguous section source feature");found=&feature;}
    if(!found||found->suppressed)throw std::runtime_error("Section source feature is missing or suppressed");const Body* body=nullptr;for(const auto& candidate:document.evalBodies)if(candidate.id==found->id)body=&candidate;
    const V* encoded=body?body->nativeData.find("brep_base64"):nullptr;const V* report=body?body->nativeData.find("report"):nullptr;
    if(!encoded||encoded->t!=V::Str||encoded->str.empty()||!report||!report->find("solids")||report->find("solids")->asNum()!=1)throw std::runtime_error("Section source requires one evaluated exact native solid");return *found;
}
V sectionCommand(const Feature& feature,const std::string& name,const std::string& datum,const std::string& mode){
    if(mode!="contour"&&mode!="positive"&&mode!="negative"&&mode!="both")throw std::runtime_error("Section mode must be contour, positive, negative or both");V command=V::makeObj(),native=V::makeObj(),inputs=V::makeArr();command.set("op",V::makeStr("feature.add"));command.set("name",V::makeStr(name));inputs.arr.push_back(V::makeNum((double)feature.id));command.set("inputs",inputs);
    native.set("plane",V::makeStr(datum));if(mode=="contour")command.set("type",V::makeStr("intersection_curve"));else{command.set("type",V::makeStr("native_solid"));native.set("op",V::makeStr("split_plane"));native.set("keep",V::makeStr(mode));}command.set("native",native);return command;
}
}
V planeSectionRecipe(const Document& document,const V& arguments){
    const auto action=text(arguments,"action");V result=V::makeObj(),commands=V::makeArr();result.set("action",V::makeStr(action));
    if(action=="sketch_to_plane"){
        keys(arguments,{"action","sketch","plane"});const auto sketchName=text(arguments,"sketch"),planeName=text(arguments,"plane");plane(document,planeName);const auto* source=namedSketch(document,sketchName);if(!source||!source->error.empty())throw std::runtime_error("Sketch is missing or invalid: "+sketchName);V sketch;if(!arkjson::parse(sketchToJson(source->sk),sketch))throw std::runtime_error("Cannot retain sketch authoring recipe");sketch.set("plane",V::makeStr(planeName));V command=V::makeObj();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(sketchName));command.set("sketch",sketch);commands.arr.push_back(command);result.set("scope",V::makeStr("Retains all authored local sketch coordinates, expressions, entities, constraints and trims; changes its saved plane. Dependent geometry must pass the atomic edit."));
    }else if(action=="create_plane"){
        keys(arguments,{"action","name","parent","p"});const auto name=text(arguments,"name"),parent=arguments.find("parent")?text(arguments,"parent"):"XY";newDatumName(document,name);plane(document,parent);DatumPlane datum;datum.name=name;datum.parent=parent;V command=V::makeObj();command.set("op",V::makeStr("plane.set"));command.set("name",V::makeStr(name));command.set("parent",V::makeStr(parent));if(const auto* parameters=arguments.find("p")){if(parameters->t!=V::Obj)throw std::runtime_error("Plane parameters must be an object");for(const auto& entry:parameters->obj){if(entry.second.t!=V::Str&&entry.second.t!=V::Num)throw std::runtime_error("Plane parameter must be an expression");datum.p[entry.first]=entry.second.t==V::Str?entry.second.str:entry.second.dump();}command.set("p",*parameters);}validateDatumDefinition(datum);commands.arr.push_back(command);result.set("plane",V::makeStr(name));result.set("scope",V::makeStr("Saved parent-relative construction plane with editable origin and Euler angles; offsets are millimetres and rotations degrees."));
    }else if(action=="offset_plane"){
        keys(arguments,{"action","name","plane","offset"});const auto name=text(arguments,"name"),sourcePlane=text(arguments,"plane");plane(document,sourcePlane);newDatumName(document,name);const auto& offset=field(arguments,"offset");bool valid=false;double value=0;if(offset.t==V::Num){value=offset.num;valid=true;}else if(offset.t==V::Str&&!offset.str.empty())value=evalExpr(offset.str,document.buildEnv(),&valid);if(!valid||!std::isfinite(value)||std::abs(value)>1e7)throw std::runtime_error("Plane offset must be a finite supported expression in millimetres");V support=V::makeObj();support.set("kind",V::makeStr("offset_plane"));support.set("plane",V::makeStr(sourcePlane));support.set("offset",offset);commands.arr.push_back(datumCommand(name,"plane",support));result.set("plane",V::makeStr(name));result.set("scope",V::makeStr("Associative signed offset along the source plane normal; retains the caller expression."));
    }else if(action=="section"||action=="section_from_line"){
        keys(arguments,action=="section"?std::set<std::string>{"action","id","name","plane","mode"}:std::set<std::string>{"action","id","name","sketch","entity","plane_name","mode"});const auto& source=sourceFeature(document,field(arguments,"id"));const auto name=text(arguments,"name");newFeatureName(document,name);const auto mode=arguments.find("mode")?text(arguments,"mode"):"contour";std::string sectionPlane;
        if(action=="section"){sectionPlane=text(arguments,"plane");plane(document,sectionPlane);}
        else{
            const auto sketchName=text(arguments,"sketch"),entityName=text(arguments,"entity");const auto* sketch=namedSketch(document,sketchName);if(!sketch||!sketch->error.empty())throw std::runtime_error("Section sketch is missing or invalid");const SketchEnt* line=nullptr;for(const auto& entity:sketch->sk.ents)if(entity.name==entityName){if(line)throw std::runtime_error("Ambiguous section sketch entity");line=&entity;}
            if(!line||line->kind!=SketchEnt::Line||line->dof.size()!=4)throw std::runtime_error("Section from line requires one authored straight sketch line");plane(document,sketch->sk.plane);
            const auto start=endpoint(sketchName,entityName,"start"),end=endpoint(sketchName,entityName,"end");if((datumAnchorPoint(document,start)-datumAnchorPoint(document,end)).len()<=1e-6)throw std::runtime_error("Section line endpoints must be distinct at fixed 1e-6 mm tolerance");sectionPlane=arguments.find("plane_name")?text(arguments,"plane_name"):name+".plane";const auto axisName=sectionPlane+".axis";newDatumName(document,sectionPlane);newDatumName(document,axisName);
            V axis=V::makeObj();axis.set("kind",V::makeStr("axis_points"));axis.set("start",start);axis.set("end",end);commands.arr.push_back(datumCommand(axisName,"axis",axis));V rotated=V::makeObj();rotated.set("kind",V::makeStr("angle_plane"));rotated.set("plane",V::makeStr(sketch->sk.plane));rotated.set("axis",V::makeStr(axisName));rotated.set("angle",V::makeNum(90));commands.arr.push_back(datumCommand(sectionPlane,"plane",rotated));
        }
        commands.arr.push_back(sectionCommand(source,name,sectionPlane,mode));result.set("plane",V::makeStr(sectionPlane));result.set("source_feature",V::makeNum((double)source.id));result.set("mode",V::makeStr(mode));result.set("scope",V::makeStr(mode=="contour"?"Exact retained-solid/plane intersection; one connected wire with at most 128 edges. Does not remove material. Disconnected sections reject.":"Exact split by an infinite plane retaining the requested signed half-space or both solids; source body retained. A sketch line defines the infinite plane, not a finite-length kerf."));
    }else throw std::runtime_error("Unsupported plane/section action: "+action);
    result.set("commands",commands);return result;
}
}
