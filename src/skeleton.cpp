#include "ark/skeleton.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
struct Declaration {std::string kind,name,expression,count,source;std::vector<std::string> vector;size_t line=0;};
std::string trim(std::string text){const auto a=text.find_first_not_of(" \t\r\n"),b=text.find_last_not_of(" \t\r\n");return a==std::string::npos?"":text.substr(a,b-a+1);}
bool identifier(const std::string& text){if(text.empty()||text.size()>64||(!std::isalpha((unsigned char)text[0])&&text[0]!='_'))return false;for(char c:text)if(!std::isalnum((unsigned char)c)&&c!='_')return false;return text!="pi"&&text!="e"&&text!="index";}
std::string named(std::string text){text=trim(text);if(text.size()>=2&&text.front()=='"'){V value;if(!arkjson::parse(text,value)||value.t!=V::Str||value.str.empty()||value.str.size()>128)throw std::runtime_error("Invalid quoted feature name");return value.str;}if(!identifier(text))throw std::runtime_error("Expected an identifier or quoted feature name");return text;}
std::vector<std::string> vectorExpressions(std::string text){
    text=trim(text);if(text.size()<5||text.front()!='['||text.back()!=']')throw std::runtime_error("Expected [x, y, z] expressions");
    text=text.substr(1,text.size()-2);std::vector<std::string> parts;int depth=0;size_t start=0;
    for(size_t i=0;i<text.size();++i){if(text[i]=='('||text[i]=='[')++depth;else if(text[i]==')'||text[i]==']'){if(--depth<0)throw std::runtime_error("Unbalanced vector expression");}else if(text[i]==','&&depth==0){parts.push_back(trim(text.substr(start,i-start)));start=i+1;}}
    parts.push_back(trim(text.substr(start)));if(depth!=0||parts.size()!=3)throw std::runtime_error("A skeleton vector needs exactly three expressions");for(const auto& expr:parts)if(expr.empty()||expr.size()>2048)throw std::runtime_error("Empty or oversized vector expression");return parts;
}
std::vector<Declaration> parse(const std::string& source){
    if(source.size()>65536||source.find('\0')!=std::string::npos)throw std::runtime_error("Skeleton source must be text of at most 64 KiB");
    std::vector<Declaration> out;std::set<std::string> names;std::istringstream lines(source);std::string text;size_t line=0;
    while(std::getline(lines,text)){++line;bool quoted=false,escaped=false;size_t comment=std::string::npos;for(size_t i=0;i<text.size();++i){if(text[i]=='"'&&!escaped)quoted=!quoted;if(!quoted&&(text[i]=='#'||(text[i]=='/'&&i+1<text.size()&&text[i+1]=='/'))){comment=i;break;}escaped=text[i]=='\\'&&!escaped;}if(comment!=std::string::npos)text.resize(comment);text=trim(text);if(!text.empty()&&text.back()==';')text=trim(text.substr(0,text.size()-1));if(text.empty())continue;
        try{
            if(out.size()>=256)throw std::runtime_error("At most 256 declarations per skeleton");
            const auto space=text.find(' ');if(space==std::string::npos)throw std::runtime_error("Expected input, relation, array or pattern declaration");Declaration d;d.kind=text.substr(0,space);d.line=line;std::string rest=trim(text.substr(space+1));
            if(d.kind=="input"||d.kind=="relation"){
                const auto equals=rest.find('=');if(equals==std::string::npos)throw std::runtime_error("Expected name = expression");d.name=trim(rest.substr(0,equals));d.expression=trim(rest.substr(equals+1));if(!identifier(d.name)||d.expression.empty()||d.expression.size()>2048)throw std::runtime_error("Invalid scalar name or expression");
            }else if(d.kind=="array"){
                const auto open=rest.find('[');if(open==std::string::npos)throw std::runtime_error("Expected array name[count] = [x,y,z]");d.name=trim(rest.substr(0,open));int depth=1;size_t close=open+1;for(;close<rest.size();++close){if(rest[close]=='[')++depth;else if(rest[close]==']'&&!--depth)break;}if(close==rest.size()||!identifier(d.name))throw std::runtime_error("Invalid array count or name");d.count=trim(rest.substr(open+1,close-open-1));rest=trim(rest.substr(close+1));if(rest.empty()||rest.front()!='=')throw std::runtime_error("Array requires = [x,y,z]");d.vector=vectorExpressions(rest.substr(1));
            }else if(d.kind=="pattern"){
                const auto from=rest.find(" from ");if(from==std::string::npos)throw std::runtime_error("Expected pattern name from source count expression step [x,y,z]");d.name=named(rest.substr(0,from));rest=rest.substr(from+6);size_t count=std::string::npos;bool quotedName=false,escape=false;for(size_t i=0;i<rest.size();++i){if(rest[i]=='"'&&!escape)quotedName=!quotedName;if(!quotedName&&rest.compare(i,7," count ")==0){count=i;break;}escape=rest[i]=='\\'&&!escape;}if(count==std::string::npos)throw std::runtime_error("Pattern requires count");d.source=named(rest.substr(0,count));rest=rest.substr(count+7);const auto step=rest.find(" step ");if(step==std::string::npos)throw std::runtime_error("Pattern requires step [x,y,z]");d.count=trim(rest.substr(0,step));d.vector=vectorExpressions(rest.substr(step+6));
            }else throw std::runtime_error("Unknown skeleton declaration: "+d.kind);
            if(!names.insert(d.name).second)throw std::runtime_error("Duplicate skeleton declaration: "+d.name);out.push_back(std::move(d));
        }catch(const std::exception& error){throw std::runtime_error("Skeleton line "+std::to_string(line)+": "+error.what());}
    }
    return out;
}
std::string indexExpression(const std::string& expression,size_t index){
    std::string result;for(size_t i=0;i<expression.size();){if(std::isalpha((unsigned char)expression[i])||expression[i]=='_'){const size_t start=i++;while(i<expression.size()&&(std::isalnum((unsigned char)expression[i])||expression[i]=='_'))++i;const auto token=expression.substr(start,i-start);result+=token=="index"?"("+std::to_string(index)+")":token;}else result+=expression[i++];}return result;
}
bool owned(const V& manifest,const char* group,const std::string& name){const auto* entries=manifest.find(group);if(entries)for(const auto& row:entries->arr)if(const auto* n=row.find("name");n&&n->str==name)return true;return false;}
V manifestOf(const Document& d){const auto* manifest=d.skeleton.find("manifest");return manifest?*manifest:V::makeObj();}
ExprEnv resolvedEnvironment(const Document& d){
    // Unresolved Document parameters carry a display placeholder of zero.
    // Counts must never treat that placeholder as an authored scalar value.
    ExprEnv env;
    for(size_t pass=0;pass<d.params.size();++pass){bool progress=false;for(const auto& p:d.params)if(!env.vars.count(p.name)){bool ok=false;const auto value=evalExpr(p.expr,env,&ok);if(ok&&std::isfinite(value)){env.vars[p.name]=value;progress=true;}}if(!progress)break;}
    return env;
}
void compile(Document& d,const std::string& source,bool resetDefaults){
    validateSkeletonAuthoring(d);
    const auto declarations=parse(source);const V prior=manifestOf(d);V overrides=V::makeObj();if(const auto* old=d.skeleton.find("overrides"))overrides=*old;
    if(resetDefaults){
        // Preserve a user's input overrides when unrelated source lines change.
        // An explicitly changed input default takes precedence over its old override.
        std::map<std::string,std::string> oldInputs;
        if(const auto* old=d.skeleton.find("source");old&&old->t==V::Str)for(const auto& decl:parse(old->str))if(decl.kind=="input")oldInputs[decl.name]=decl.expression;
        V retained=V::makeObj();for(const auto& decl:declarations)if(decl.kind=="input")if(const auto* value=overrides.find(decl.name.c_str()))if(oldInputs.count(decl.name)&&oldInputs.at(decl.name)==decl.expression)retained.set(decl.name,*value);overrides=std::move(retained);
    }
    std::map<std::string,Feature> oldFeatures;
    if(const auto* rows=prior.find("features"))for(const auto& row:rows->arr){const auto* name=row.find("name"),*id=row.find("id");if(!name||name->t!=V::Str||!id||id->t!=V::Num||id->num<1||id->num>9007199254740990.||id->num!=std::floor(id->num))throw std::runtime_error("Invalid skeleton feature ownership");const Feature* feature=d.find((u64)id->num);if(!feature||feature->name!=name->str||feature->type!=FeatureType::LinearPattern)throw std::runtime_error("Skeleton feature ownership changed; edit the skeleton source to restore its outputs");if(!oldFeatures.emplace(name->str,*feature).second)throw std::runtime_error("Duplicate skeleton feature ownership");}
    d.params.erase(std::remove_if(d.params.begin(),d.params.end(),[&](const Parameter& p){return owned(prior,"parameters",p.name);}),d.params.end());
    std::set<std::string> managedNames;V parameters=V::makeArr(),arrays=V::makeArr(),features=V::makeArr(),keptOverrides=V::makeObj();
    auto parameter=[&](const std::string& name,const std::string& expression,const char* kind){
        if(!managedNames.insert(name).second)throw std::runtime_error("Generated skeleton parameter collision: "+name);
        for(const auto& existing:d.params)if(existing.name==name)throw std::runtime_error("Skeleton would replace an unowned parameter: "+name);
        if(d.params.size()>=256)throw std::runtime_error("Skeleton expansion exceeds the document capacity of 256 parameters");
        d.params.push_back({name,expression,0});V row=V::makeObj();row.set("name",V::makeStr(name));row.set("kind",V::makeStr(kind));parameters.arr.push_back(row);
    };
    for(const auto& decl:declarations)if(decl.kind=="input"||decl.kind=="relation"){
        std::string expression=decl.expression;if(decl.kind=="input")if(const auto* value=overrides.find(decl.name.c_str())){if(value->t!=V::Str)throw std::runtime_error("Skeleton input override must be an expression");expression=value->str;keptOverrides.set(decl.name,*value);}parameter(decl.name,expression,decl.kind.c_str());
    }
    d.refreshParams();
    auto count=[&](const Declaration& decl){bool ok=false;const auto value=evalExpr(decl.count,resolvedEnvironment(d),&ok);if(!ok||value<1||value>256||value!=std::floor(value))throw std::runtime_error("Skeleton line "+std::to_string(decl.line)+": count must resolve before expansion to an integer in 1..256");return (size_t)value;};
    for(const auto& decl:declarations)if(decl.kind=="array"){
        const size_t n=count(decl);parameter(decl.name+"_count",decl.count,"array_count");
        for(size_t i=0;i<n;++i)for(size_t axis=0;axis<3;++axis)parameter(decl.name+"_"+std::to_string(i)+"_"+std::string(1,"xyz"[axis]),indexExpression(decl.vector[axis],i),"array_coordinate");
        d.refreshParams();V row=V::makeObj();row.set("name",V::makeStr(decl.name));row.set("count",V::makeNum(n));row.set("line",V::makeNum(decl.line));arrays.arr.push_back(row);
    }
    if(!d.parameterError.empty())throw std::runtime_error("Skeleton parameters: "+d.parameterError);
    std::set<u64> retained;
    for(const auto& decl:declarations)if(decl.kind=="pattern"){
        count(decl);u64 input=0;for(const auto& candidate:d.features)if(candidate.name==decl.source){if(input)throw std::runtime_error("Ambiguous skeleton pattern source: "+decl.source);input=candidate.id;}if(!input)throw std::runtime_error("Skeleton line "+std::to_string(decl.line)+": source feature not found: "+decl.source);
        Feature feature;const auto previous=oldFeatures.find(decl.name);if(previous!=oldFeatures.end())feature=previous->second;else{for(const auto& candidate:d.features)if(candidate.name==decl.name)throw std::runtime_error("Skeleton would replace an unowned feature: "+decl.name);feature.name=decl.name;feature.type=FeatureType::LinearPattern;}
        if(feature.id==input)throw std::runtime_error("Skeleton pattern cannot reference itself");
        feature.inputs={input};feature.p={{"count",decl.count},{"dx",decl.vector[0]},{"dy",decl.vector[1]},{"dz",decl.vector[2]}};
        if(feature.id){*d.find(feature.id)=feature;}else feature.id=d.addFeature(feature);retained.insert(feature.id);V row=V::makeObj();row.set("id",V::makeNum((double)feature.id));row.set("name",V::makeStr(feature.name));features.arr.push_back(row);
    }
    for(const auto& previous:oldFeatures)if(!retained.count(previous.second.id))d.remove(previous.second.id);
    V manifest=V::makeObj();manifest.set("parameters",parameters);manifest.set("arrays",arrays);manifest.set("features",features);d.skeleton=V::makeObj();d.skeleton.set("language",V::makeStr("ark.skeleton.v1"));d.skeleton.set("source",V::makeStr(source));d.skeleton.set("overrides",keptOverrides);d.skeleton.set("manifest",manifest);d.refreshParams();
}
}
bool skeletonDocument(const Document& d){return d.skeleton.t!=V::Null;}
void validateSkeletonAuthoring(const Document& d){
    if(!skeletonDocument(d))return;
    const auto* language=d.skeleton.find("language"),*source=d.skeleton.find("source"),*manifest=d.skeleton.find("manifest"),*overrides=d.skeleton.find("overrides");
    if(d.skeleton.t!=V::Obj||!language||language->t!=V::Str||language->str!="ark.skeleton.v1"||!source||source->t!=V::Str||!manifest||manifest->t!=V::Obj||!overrides||overrides->t!=V::Obj)throw std::runtime_error("Invalid skeleton source/ownership object");
    for(const auto& entry:d.skeleton.obj)if(entry.first!="language"&&entry.first!="source"&&entry.first!="manifest"&&entry.first!="overrides")throw std::runtime_error("Unknown skeleton field");
    for(const auto& entry:manifest->obj)if(entry.first!="parameters"&&entry.first!="arrays"&&entry.first!="features")throw std::runtime_error("Unknown skeleton ownership group");
    const auto declarations=parse(source->str);
    std::map<std::string,std::string> expectedParameters;std::set<std::string> expectedFeatures,inputNames,arrayNames;
    for(const auto& decl:declarations){if(decl.kind=="input"||decl.kind=="relation"){expectedParameters.emplace(decl.name,decl.kind);if(decl.kind=="input")inputNames.insert(decl.name);}else if(decl.kind=="array")arrayNames.insert(decl.name);else expectedFeatures.insert(decl.name);}
    auto rows=[&](const char* key)->const V&{const auto* value=manifest->find(key);if(!value||value->t!=V::Arr||value->arr.size()>2048)throw std::runtime_error("Invalid skeleton ownership rows");return *value;};
    std::set<std::string> seenArrays;
    for(const auto& row:rows("arrays").arr){const auto* name=row.find("name"),*count=row.find("count"),*line=row.find("line");if(row.t!=V::Obj||!name||name->t!=V::Str||!arrayNames.count(name->str)||!seenArrays.insert(name->str).second||!count||count->t!=V::Num||!std::isfinite(count->num)||count->num<1||count->num>256||count->num!=std::floor(count->num)||!line||line->t!=V::Num||line->num<1||line->num!=std::floor(line->num))throw std::runtime_error("Invalid skeleton array ownership");
        if(!expectedParameters.emplace(name->str+"_count","array_count").second)throw std::runtime_error("Skeleton generated parameter collision");
        for(size_t i=0;i<(size_t)count->num;++i)for(char axis:std::string("xyz"))if(!expectedParameters.emplace(name->str+"_"+std::to_string(i)+"_"+axis,"array_coordinate").second)throw std::runtime_error("Skeleton generated parameter collision");
    }
    if(seenArrays!=arrayNames)throw std::runtime_error("Skeleton array ownership does not match source");
    std::set<std::string> seenParameters;
    for(const auto& row:rows("parameters").arr){const auto* name=row.find("name"),*kind=row.find("kind");if(row.t!=V::Obj||!name||name->t!=V::Str||!kind||kind->t!=V::Str||!seenParameters.insert(name->str).second||!expectedParameters.count(name->str)||expectedParameters.at(name->str)!=kind->str)throw std::runtime_error("Skeleton parameter ownership does not match source");bool found=false;for(const auto& param:d.params)if(param.name==name->str)found=true;if(!found)throw std::runtime_error("Missing skeleton-owned parameter: "+name->str);}
    if(seenParameters.size()!=expectedParameters.size())throw std::runtime_error("Incomplete skeleton parameter ownership");
    std::set<std::string> seenFeatures;std::set<u64> ids;
    for(const auto& row:rows("features").arr){const auto* name=row.find("name"),*id=row.find("id");if(row.t!=V::Obj||!name||name->t!=V::Str||!expectedFeatures.count(name->str)||!seenFeatures.insert(name->str).second||!id||id->t!=V::Num||!std::isfinite(id->num)||id->num<1||id->num>9007199254740990.||id->num!=std::floor(id->num)||!ids.insert((u64)id->num).second)throw std::runtime_error("Invalid skeleton feature ownership");const auto* feature=d.find((u64)id->num);if(!feature||feature->name!=name->str||feature->type!=FeatureType::LinearPattern)throw std::runtime_error("Skeleton feature ownership changed; edit its source instead");}
    if(seenFeatures!=expectedFeatures)throw std::runtime_error("Skeleton feature ownership does not match source");
    for(const auto& entry:overrides->obj)if(!inputNames.count(entry.first)||entry.second.t!=V::Str||entry.second.str.empty()||entry.second.str.size()>2048)throw std::runtime_error("Invalid skeleton input override");
}
void refreshSkeleton(Document& d){if(!skeletonDocument(d))return;if(d.skeleton.t!=V::Obj)throw std::runtime_error("Invalid skeleton authoring object");const auto* language=d.skeleton.find("language"),*source=d.skeleton.find("source");if(!language||language->str!="ark.skeleton.v1"||!source||source->t!=V::Str)throw std::runtime_error("Unsupported skeleton language or source");compile(d,source->str,false);}
void setSkeletonSource(Document& d,const std::string& source){compile(d,source,true);}
bool skeletonOwnsParameter(const Document& d,const std::string& name){return owned(manifestOf(d),"parameters",name);}
bool skeletonOwnsFeature(const Document& d,u64 id){const auto manifest=manifestOf(d);const auto* rows=manifest.find("features");if(rows)for(const auto& row:rows->arr)if(row.find("id")&&row.find("id")->num==(double)id)return true;return false;}
bool setSkeletonInput(Document& d,const std::string& name,const std::string& expression){
    if(!skeletonOwnsParameter(d,name))return false;const V manifest=manifestOf(d);const auto* rows=manifest.find("parameters");bool input=false;for(const auto& row:rows->arr)if(row.find("name")->str==name)input=row.find("kind")->str=="input";if(!input)throw std::runtime_error("Skeleton relation/array is source-owned; edit it in Skeleton code");V overrides=d.skeleton.find("overrides")?*d.skeleton.find("overrides"):V::makeObj();overrides.set(name,V::makeStr(expression));d.skeleton.set("overrides",overrides);return true;
}
V inspectSkeleton(const Document& d){V out=V::makeObj();out.set("present",V::makeBool(skeletonDocument(d)));out.set("authoring",d.skeleton);out.set("execution",V::makeStr("native_declarative"));out.set("length_units",V::makeStr("mm"));V parameters=V::makeArr();for(const auto& p:d.params)if(skeletonOwnsParameter(d,p.name)){V row=V::makeObj();row.set("name",V::makeStr(p.name));row.set("expression",V::makeStr(p.expr));row.set("value",V::makeNum(p.value));parameters.arr.push_back(row);}out.set("parameters",parameters);return out;}
V compileSkeletonPreview(const Document& document,const std::string& source){Document candidate=document.authoringCopy();V out=V::makeObj();try{setSkeletonSource(candidate,source);out=inspectSkeleton(candidate);out.set("valid",V::makeBool(true));out.set("geometry_evaluated",V::makeBool(false));out.set("note",V::makeStr("Syntax/relations preview; doc_preview validates dependent geometry before atomic application"));}catch(const std::exception& error){out.set("valid",V::makeBool(false));out.set("error",V::makeStr(error.what()));}out.set("document_unchanged",V::makeBool(true));return out;}
std::string skeletonExample(){return "# Native parametric skeleton; lengths mm\ninput span = 120\ninput rib_count = 4\nrelation pitch = span / max(rib_count - 1, 1)\nrelation thickness = span / 30\narray stations[rib_count] = [index * pitch, 0, thickness]\n# Add a seed feature, then enable: \n# pattern Ribs from Seed count rib_count step [pitch, 0, 0]\n# Features can use stations[2].x and thickness in their dimensions.\n";}
}
