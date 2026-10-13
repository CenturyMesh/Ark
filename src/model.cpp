// ARK-Point :: model.cpp — expressions, parameters, document bookkeeping.
#include "ark/model.hpp"
#include "ark/manufacturing.hpp"
#include <cctype>
#include <cmath>

namespace ark {

std::string featureOperation(const Feature& feature){
    if(feature.type!=FeatureType::NativeSolid)return featureTypeName(feature.type);
    std::string operation=feature.get("op");
    if(operation.empty())if(const auto* value=feature.nativeSpec.find("op"))if(value->t==arkjson::Value::Str)operation=value->str;
    if(operation=="rigid_transform")return "transform";
    if(operation=="boolean_union")return "union";
    if(operation=="boolean_cut")return "cut";
    if(operation=="boolean_common"||operation=="intersect")return "common";
    if(operation=="pocket"||operation=="slot")return "hole";
    return operation;
}

ManufacturingLineage featureManufacturingLineage(const Document& document,const Feature& feature){
    const Feature* current=&feature;
    // History is a DAG. Bound traversal also makes read-only inspection safe for
    // a malformed document that has not passed recomputation yet.
    for(size_t depth=0;depth<=document.features.size();++depth){
        if(current->type==FeatureType::SheetMetal)return {"sheet_metal",current->id,true};
        if(current->type==FeatureType::SourceSheet)return {"sheet_metal",current->id,false};
        if(current->type==FeatureType::ConstructionFrame)return {"frame",current->id,false};
        const bool placed=current->type==FeatureType::NativeSolid&&featureOperation(*current)=="transform";
        if(!placed||current->inputs.size()!=1)break;
        const Feature* parent=document.find(current->inputs[0]);if(!parent||parent==current)break;
        current=parent;
    }
    if(isMeshModelFeature(current->type)||current->type==FeatureType::ParametricSurface)return {"mesh",0,false};
    if(isCurveFeature(current->type))return {"curve",0,false};
    if(current->type==FeatureType::SurfaceFill||current->type==FeatureType::BSplineSurface||current->type==FeatureType::OffsetSurface||current->type==FeatureType::SurfaceEdit||
       (isNativeSurfaceFeature(current->type)&&current->type!=FeatureType::Thicken&&current->num(document.buildEnv(),"solid",1)==0))return {"surface",0,false};
    if(current->type==FeatureType::PCB)return {"electronics",0,false};
    if(current->type==FeatureType::CableRoute)return {"routing",0,false};
    if(current->type==FeatureType::Suspension)return {"kinematic_model",0,false};
    // A native placement explicitly promotes supported analytic inputs to an
    // exact B-rep. Unsupported promotion still fails normal recomputation.
    if(feature.type==FeatureType::NativeSolid&&current!=&feature)return {"solid",0,false};
    if(current->type==FeatureType::NativeSolid||current->type==FeatureType::Tube||current->type==FeatureType::NotchedTube||current->type==FeatureType::SourceTube||isNativeSurfaceFeature(current->type))return {"solid",0,false};
    if(current->type==FeatureType::Box||current->type==FeatureType::Cylinder||current->type==FeatureType::Sphere||current->type==FeatureType::Cone||current->type==FeatureType::Torus||current->type==FeatureType::Extrude||current->type==FeatureType::Revolve||current->type==FeatureType::Transform||current->type==FeatureType::Boolean||current->type==FeatureType::LinearPattern||current->type==FeatureType::SketchCut)return {"mesh_solid",0,false};
    return {};
}
bool validateManufacturingRoutes(const Document& document,std::string& error){
    for(const auto& feature:document.features){
        const auto& declared=feature.manufacturingRoute;
        if(!validateManufacturingProcess(feature,error))return false;
        if(declared.empty()||declared=="unspecified"||declared=="purchased")continue;
        if(!isManufacturingRoute(declared)){
            error="Feature #"+std::to_string(feature.id)+": unknown manufacturing route";return false;
        }
        const auto lineage=featureManufacturingLineage(document,feature);
        if((declared=="sheet_metal"&&lineage.geometryRoute!="sheet_metal")||
           (declared=="frame"&&lineage.geometryRoute!="frame")||
           ((declared=="machined"||declared=="injection_molding"||declared=="additive")&&lineage.geometryRoute!="solid")){
            error="Feature #"+std::to_string(feature.id)+": declared manufacturing route '"+declared+"' does not match structured geometry lineage '"+lineage.geometryRoute+"'. Use sheet.create/sheet edits for sheet metal, construction_frame for frames, exact solid operations for machining/injection/additive, or purchased for a supplier envelope.";
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Expression evaluator ---------------------------------------------------------
namespace {

struct ExprParser {
    const std::string& s; size_t i=0;
    const ExprEnv& env;
    bool ok=true;

    ExprParser(const std::string& src, const ExprEnv& e):s(src),env(e){}

    void ws(){ while(i<s.size() && (s[i]==' '||s[i]=='\t')) ++i; }
    bool eat(char c){ ws(); if(i<s.size()&&s[i]==c){++i;return true;} return false; }

    f64 expr(){
        f64 v=term();
        for(;;){
            if(eat('+')) v+=term();
            else if(eat('-')) v-=term();
            else return v;
        }
    }
    f64 term(){
        f64 v=power();
        for(;;){
            if(eat('*')) v*=power();
            else if(eat('/')){
                f64 d=power();
                if(std::fabs(d)<1e-300){ ok=false; return 0; }
                v/=d;
            }
            else return v;
        }
    }
    f64 power(){
        f64 b=unary();
        if(eat('^')) return std::pow(b,power());
        return b;
    }
    f64 unary(){
        if(eat('-')) return -unary();
        if(eat('+')) return unary();
        return atom();
    }
    f64 atom(){
        ws();
        if(i<s.size() && s[i]=='('){
            ++i;
            f64 v=expr();
            if(!eat(')')) ok=false;
            return v;
        }
        size_t j=i;
        if(i<s.size() && (isdigit((unsigned char)s[i])||s[i]=='.')){
            char* end=nullptr;
            f64 v=strtod(s.c_str()+i,&end);
            if(end==s.c_str()+i){ok=false; return 0;}
            i=(size_t)(end-s.c_str()); return v;
        }
        while(j<s.size() && (isalnum((unsigned char)s[j])||s[j]=='_')) ++j;
        if(j==i){ ok=false; return 0; }
        std::string id=s.substr(i,j-i);
        i=j;
        ws();
        if(eat('[')){
            const f64 index=expr();if(!eat(']')||index<0||index>255||index!=std::floor(index)){ok=false;return 0;}
            if(!eat('.')){ok=false;return 0;}ws();if(i>=s.size()||(s[i]!='x'&&s[i]!='y'&&s[i]!='z')){ok=false;return 0;}
            id+="_"+std::to_string((int)index)+"_"+s[i++];ws();
        }
        if(i<s.size() && s[i]=='('){
            ++i;
            std::vector<f64> args{expr()};
            while(eat(',')) args.push_back(expr());
            if(!eat(')')) ok=false;
            return callFn(id,args);
        }
        auto it=env.vars.find(id);
        if(it!=env.vars.end()) return it->second;
        if(id=="pi") return K_PI;
        if(id=="e")  return 2.71828182845904523536;
        ok=false; return 0;
    }
    f64 callFn(const std::string& id,const std::vector<f64>& a){
        const bool binary=id=="atan2"||id=="min"||id=="max"||id=="pow";
        if(a.size()!=(binary?2u:1u)){ok=false; return 0;}
        auto g=[&](size_t k,f64 d=0){ return a.size()>k?a[k]:d; };
        if(id=="sin")   return std::sin(g(0));
        if(id=="cos")   return std::cos(g(0));
        if(id=="tan")   return std::tan(g(0));
        if(id=="asin")  return std::asin(g(0));
        if(id=="acos")  return std::acos(g(0));
        if(id=="atan2") return std::atan2(g(0),g(1));
        if(id=="sqrt")  return std::sqrt(g(0));
        if(id=="abs")   return std::fabs(g(0));
        if(id=="min")   return tmin(g(0),g(1,g(0)));
        if(id=="max")   return tmax(g(0),g(1,g(0)));
        if(id=="pow")   return std::pow(g(0),g(1));
        if(id=="floor") return std::floor(g(0));
        if(id=="ceil")  return std::ceil(g(0));
        if(id=="deg")   return deg2rad(g(0));      // degrees -> radians
        ok=false; return 0;
    }
};

} // namespace

f64 evalExpr(const std::string& src, const ExprEnv& env, bool* okOut) {
    if(src.size()>2048){if(okOut)*okOut=false;return 0;}
    char* end=nullptr;
    const char* c=src.c_str();
    f64 v=strtod(c,&end);
    if(end && *end=='\0' && end!=c){ if(okOut)*okOut=std::isfinite(v); return v; }
    ExprParser p(src,env);
    f64 r=p.expr();
    p.ws();
    if(okOut)*okOut = p.ok && p.i>=src.size() && std::isfinite(r);
    return r;
}

bool Parameter::depends(const std::string& var) const {
    return expr.find(var)!=std::string::npos;
}

// ---------------------------------------------------------------------------
// Feature type names -----------------------------------------------------------
const char* featureTypeName(FeatureType t){
    switch(t){
        case FeatureType::NativeSolid:   return "native_solid";
        case FeatureType::Box:           return "box";
        case FeatureType::Cylinder:      return "cylinder";
        case FeatureType::Sphere:        return "sphere";
        case FeatureType::Cone:          return "cone";
        case FeatureType::Torus:         return "torus";
        case FeatureType::Extrude:       return "extrude";
        case FeatureType::Revolve:       return "revolve";
        case FeatureType::Transform:     return "transform";
        case FeatureType::Boolean:       return "boolean";
        case FeatureType::LinearPattern: return "pattern";
        case FeatureType::ParametricSurface: return "surface";
        case FeatureType::Tube: return "tube";
        case FeatureType::NotchedTube: return "notched_tube";
        case FeatureType::SourceTube: return "source_tube";
        case FeatureType::SketchCut: return "sketch_cut";
        case FeatureType::Loft: return "loft";
        case FeatureType::SurfaceFill: return "surface_fill";
        case FeatureType::BSplineSurface: return "bspline_surface";
        case FeatureType::Thicken: return "thicken";
        case FeatureType::SheetMetal: return "sheet_metal";
        case FeatureType::SourceSheet: return "source_sheet";
        case FeatureType::Sweep: return "sweep";
        case FeatureType::ExtrudeSurface: return "extrude_surface";
        case FeatureType::RevolveSurface: return "revolve_surface";
        case FeatureType::OffsetSurface: return "offset_surface";
        case FeatureType::Sew: return "sew";
        case FeatureType::SurfaceEdit: return "surface_edit";
        case FeatureType::MeshBody: return "mesh_body";
        case FeatureType::MeshWeld: return "mesh_weld";
        case FeatureType::MeshSmooth: return "mesh_smooth";
        case FeatureType::MeshDecimate: return "mesh_decimate";
        case FeatureType::MeshOffset: return "mesh_offset";
        case FeatureType::MeshFill: return "mesh_fill";
        case FeatureType::MeshEdit: return "mesh_edit";
        case FeatureType::Curve: return "curve";
        case FeatureType::IntersectionCurve: return "intersection_curve";
        case FeatureType::Bridge: return "bridge";
        case FeatureType::ConstructionFrame: return "construction_frame";
        case FeatureType::PCB: return "pcb";
        case FeatureType::CableRoute: return "cable_route";
        case FeatureType::Suspension: return "suspension";
    }
    return "unknown";
}
bool isCurveFeature(FeatureType t){
    return t==FeatureType::Curve || t==FeatureType::IntersectionCurve || t==FeatureType::Bridge;
}
bool isNativeSurfaceFeature(FeatureType t){
    return t==FeatureType::Loft || t==FeatureType::SurfaceFill || t==FeatureType::BSplineSurface ||
           t==FeatureType::Thicken || t==FeatureType::Sweep || t==FeatureType::ExtrudeSurface ||
           t==FeatureType::RevolveSurface || t==FeatureType::OffsetSurface || t==FeatureType::Sew || t==FeatureType::SurfaceEdit;
}
bool isMeshModelFeature(FeatureType t){
    return t==FeatureType::MeshBody || t==FeatureType::MeshWeld || t==FeatureType::MeshSmooth ||
           t==FeatureType::MeshDecimate || t==FeatureType::MeshOffset || t==FeatureType::MeshFill || t==FeatureType::MeshEdit;
}
bool featureTypeFromName(const std::string& s, FeatureType& out){
    if(s=="box") out=FeatureType::Box;
    else if(s=="cylinder") out=FeatureType::Cylinder;
    else if(s=="sphere") out=FeatureType::Sphere;
    else if(s=="cone") out=FeatureType::Cone;
    else if(s=="torus") out=FeatureType::Torus;
    else if(s=="extrude") out=FeatureType::Extrude;
    else if(s=="revolve") out=FeatureType::Revolve;
    else if(s=="transform") out=FeatureType::Transform;
    else if(s=="boolean"||s=="union"||s=="cut"||s=="intersect"){
        out=FeatureType::Boolean; return true;
    }
    else if(s=="pattern") out=FeatureType::LinearPattern;
    else if(s=="surface") out=FeatureType::ParametricSurface;
    else if(s=="tube") out=FeatureType::Tube;
    else if(s=="notched_tube") out=FeatureType::NotchedTube;
    else if(s=="source_tube") out=FeatureType::SourceTube;
    else if(s=="sketch_cut") out=FeatureType::SketchCut;
    else if(s=="loft") out=FeatureType::Loft;
    else if(s=="surface_fill") out=FeatureType::SurfaceFill;
    else if(s=="bspline_surface") out=FeatureType::BSplineSurface;
    else if(s=="thicken") out=FeatureType::Thicken;
    else if(s=="sheet_metal") out=FeatureType::SheetMetal;
    else if(s=="source_sheet") out=FeatureType::SourceSheet;
    else if(s=="native_solid") out=FeatureType::NativeSolid;
    else if(s=="sweep") out=FeatureType::Sweep;
    else if(s=="extrude_surface") out=FeatureType::ExtrudeSurface;
    else if(s=="revolve_surface") out=FeatureType::RevolveSurface;
    else if(s=="offset_surface") out=FeatureType::OffsetSurface;
    else if(s=="sew") out=FeatureType::Sew;
    else if(s=="surface_edit") out=FeatureType::SurfaceEdit;
    else if(s=="mesh_body") out=FeatureType::MeshBody;
    else if(s=="mesh_weld") out=FeatureType::MeshWeld;
    else if(s=="mesh_smooth") out=FeatureType::MeshSmooth;
    else if(s=="mesh_decimate") out=FeatureType::MeshDecimate;
    else if(s=="mesh_offset") out=FeatureType::MeshOffset;
    else if(s=="mesh_fill") out=FeatureType::MeshFill;
    else if(s=="mesh_edit") out=FeatureType::MeshEdit;
    else if(s=="curve") out=FeatureType::Curve;
    else if(s=="intersection_curve") out=FeatureType::IntersectionCurve;
    else if(s=="bridge") out=FeatureType::Bridge;
    else if(s=="construction_frame") out=FeatureType::ConstructionFrame;
    else if(s=="pcb") out=FeatureType::PCB;
    else if(s=="cable_route") out=FeatureType::CableRoute;
    else if(s=="suspension") out=FeatureType::Suspension;
    else return false;
    return true;
}

// ---------------------------------------------------------------------------
// Sketch constraint type names -------------------------------------------------
const char* constraintTypeName(i32 t){
    switch(t){
        case SketchCon::FixedX:        return "fixedX";
        case SketchCon::FixedY:        return "fixedY";
        case SketchCon::Fixed:         return "fixed";
        case SketchCon::DistPP:        return "distance";
        case SketchCon::Horizontal:    return "horizontal";
        case SketchCon::Vertical:      return "vertical";
        case SketchCon::Parallel:      return "parallel";
        case SketchCon::Perpendicular: return "perpendicular";
        case SketchCon::Radius:        return "radius";
        case SketchCon::Coincident:    return "coincident";
        case SketchCon::AngleLL:       return "angle";
        case SketchCon::Tangent:       return "tangent";
    }
    return "unknown";
}
bool constraintTypeFromName(const std::string& s, i32& out){
    if(s=="fixedX") out=SketchCon::FixedX;
    else if(s=="fixedY") out=SketchCon::FixedY;
    else if(s=="fixed") out=SketchCon::Fixed;
    else if(s=="distance") out=SketchCon::DistPP;
    else if(s=="horizontal") out=SketchCon::Horizontal;
    else if(s=="vertical") out=SketchCon::Vertical;
    else if(s=="parallel") out=SketchCon::Parallel;
    else if(s=="perpendicular") out=SketchCon::Perpendicular;
    else if(s=="radius") out=SketchCon::Radius;
    else if(s=="coincident") out=SketchCon::Coincident;
    else if(s=="angle") out=SketchCon::AngleLL;
    else if(s=="tangent") out=SketchCon::Tangent;
    else return false;
    return true;
}

// ---------------------------------------------------------------------------
// Named sketches -----------------------------------------------------------------
NamedSketch* Document::findSketch(const std::string& name){
    for(auto&s:sketches) if(s.name==name) return &s;
    return nullptr;
}
void Document::setSketch(const std::string& name, Sketch sk){
    if(auto* s=findSketch(name)){ s->sk=std::move(sk); return; }
    sketches.push_back({name,std::move(sk)});
}
bool Document::removeSketch(const std::string& name){
    for(auto it=sketches.begin();it!=sketches.end();++it)
        if(it->name==name){ sketches.erase(it); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Document ---------------------------------------------------------------------
void Document::refreshParams() {
    parameterError.clear();
    ExprEnv env;
    std::vector<bool> resolved(params.size(),false);
    size_t remaining=params.size();
    for(size_t pass=0;pass<params.size() && remaining;++pass){
        bool progress=false;
        for(size_t i=0;i<params.size();++i){
            if(resolved[i]) continue;
            bool ok=false; f64 v=evalExpr(params[i].expr,env,&ok);
            if(ok){params[i].value=v; env.vars[params[i].name]=v; resolved[i]=true; --remaining; progress=true;}
        }
        if(!progress)break;
    }
    for(size_t i=0;i<params.size();++i) if(!resolved[i]){
        params[i].value=0;
        parameterError+="Unresolved parameter '"+params[i].name+"': cycle, unknown variable or invalid expression. ";
    }
}

void Document::setParam(const std::string& name, const std::string& expr) {
    for(auto&p:params) if(p.name==name){ p.expr=expr; refreshParams(); return; }
    Parameter np; np.name=name; np.expr=expr; np.value=0;
    params.push_back(std::move(np));
    refreshParams();
}
f64 Document::getParam(const std::string& name) const {
    for(auto&p:params) if(p.name==name) return p.value;
    return 0;
}
bool Document::removeParam(const std::string& name) {
    for(auto it=params.begin();it!=params.end();++it)
        if(it->name==name){ params.erase(it); return true; }
    return false;
}

u64 Document::addFeature(Feature f) {
    f.id=nextFeatureId_++;
    features.push_back(std::move(f));
    return features.back().id;
}
Feature* Document::find(u64 id) {
    for(auto&f:features) if(f.id==id) return &f;
    return nullptr;
}
const Feature* Document::find(u64 id) const {
    for(const auto& feature:features)if(feature.id==id)return &feature;
    return nullptr;
}
bool Document::remove(u64 id) {
    for(auto it=features.begin();it!=features.end();++it)
        if(it->id==id){ features.erase(it); return true; }
    return false;
}
bool Document::reorderMove(u64 id,int delta) {
    for(size_t i=0;i<features.size();++i){
        if(features[i].id==id){
            i64 j=(i64)i+delta;
            if(j<0||j>=(i64)features.size()) return false;
            std::swap(features[i],features[(size_t)j]);
            return true;
        }
    }
    return false;
}

bool applyAppearancePatch(Appearance& appearance,const arkjson::Value& patch,std::string& error){
    using V=arkjson::Value;
    if(patch.t!=V::Obj){error="Appearance must be an object";return false;}
    for(const auto& field:patch.obj){
        const auto& key=field.first;const V& value=field.second;
        auto scalar=[&](double& target,double lo,double hi){if(value.t!=V::Num||!std::isfinite(value.num)||value.num<lo||value.num>hi){error="Invalid appearance "+key;return false;}target=value.num;return true;};
        if(key=="base_color"){
            if(value.t!=V::Arr||value.arr.size()!=3){error="base_color needs three RGB values";return false;}
            double rgb[3];for(int i=0;i<3;++i){const auto& c=value.arr[i];if(c.t!=V::Num||!std::isfinite(c.num)||c.num<0||c.num>1){error="base_color must use sRGB display values 0..1";return false;}rgb[i]=c.num;}
            appearance.baseColor={rgb[0],rgb[1],rgb[2]};
        }else if(key=="metallic"){if(!scalar(appearance.metallic,0,1))return false;}
        else if(key=="roughness"){if(!scalar(appearance.roughness,0,1))return false;}
        else if(key=="opacity"){if(!scalar(appearance.opacity,0,1))return false;}
        else if(key=="texture_path"){
            if(value.t!=V::Str||value.str.size()>1024){error="texture_path must be a string of at most 1024 characters";return false;}
            appearance.texturePath=value.str;
        }else if(key=="texture_scale"){
            if(value.t!=V::Arr||value.arr.size()!=2){error="texture_scale needs two positive values";return false;}
            for(int i=0;i<2;++i){const V& scale=value.arr[i];if(scale.t!=V::Num||!std::isfinite(scale.num)||scale.num<=0||scale.num>1e6){error="texture_scale must be positive and at most 1e6";return false;}}
            appearance.textureScaleU=value.arr[0].num;appearance.textureScaleV=value.arr[1].num;
        }else{error="Unknown appearance property: "+key;return false;}
    }
    return true;
}

arkjson::Value appearanceToJson(const Appearance& appearance){
    using V=arkjson::Value;V out=V::makeObj(),color=V::makeArr(),scale=V::makeArr();
    for(double channel:{appearance.baseColor.x,appearance.baseColor.y,appearance.baseColor.z})color.arr.push_back(V::makeNum(channel));
    scale.arr.push_back(V::makeNum(appearance.textureScaleU));scale.arr.push_back(V::makeNum(appearance.textureScaleV));
    out.set("base_color",color);out.set("metallic",V::makeNum(appearance.metallic));out.set("roughness",V::makeNum(appearance.roughness));out.set("opacity",V::makeNum(appearance.opacity));out.set("texture_path",V::makeStr(appearance.texturePath));out.set("texture_scale",scale);return out;
}

bool validateMaterial(const Material& material,std::string& error){
    if(material.name.empty()||material.name.size()>128){error="Material name must be 1..128 characters";return false;}
    if(!std::isfinite(material.densityGcm3)||material.densityGcm3<0||material.densityGcm3>100){error="Material density must be 0..100 g/cm3 (zero means unspecified)";return false;}
    if(material.physical.t!=arkjson::Value::Null&&material.physical.t!=arkjson::Value::Obj){error="Material physical properties must be an object";return false;}
    for(const auto& item:material.physical.obj){
        const auto& key=item.first;const auto& value=item.second;
        if(value.t!=arkjson::Value::Num||!std::isfinite(value.num)){error="Physical material values must be finite SI numbers";return false;}
        bool valid=key=="young_modulus_Pa"||key=="yield_strength_Pa"?value.num>0&&value.num<=1e15:key=="poisson_ratio"?value.num>-1&&value.num<.5:key=="thermal_expansion_per_K"?std::abs(value.num)<=.01:false;
        if(!valid){error="Unsupported or invalid physical material property: "+key;return false;}
    }
    Appearance check;return applyAppearancePatch(check,appearanceToJson(material.appearance),error);
}

Appearance resolvedAppearance(const Document& document,const Feature& feature){
    Appearance result;
    if(!feature.material.empty())for(const Material& material:document.materials)if(material.name==feature.material){result=material.appearance;break;}
    if(feature.appearance.t==arkjson::Value::Obj){std::string error;applyAppearancePatch(result,feature.appearance,error);}
    return result;
}

Document Document::authoringCopy() const {
    Document out;out.params=params;out.parameterError=parameterError;out.sketches=sketches;out.planes=planes;out.canvases=canvases;
    out.features=features;out.materials=materials;out.usage=usage;out.agentActivity=agentActivity;out.bomDocumentId=bomDocumentId;out.iterationHistory=iterationHistory;out.skeleton=skeleton;
    out.assembly=assembly;out.mates=mates;out.nextMateId_=nextMateId_;out.nextAssemblyId_=nextAssemblyId_;out.nextFeatureId_=nextFeatureId_;out.tessOpt=tessOpt;
    return out;
}

} // namespace ark
