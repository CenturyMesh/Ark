// ARK-Point :: model.cpp — expressions, parameters, document bookkeeping.
#include "ark/model.hpp"
#include <cctype>

namespace ark {

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
    }
    return "unknown";
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

} // namespace ark
