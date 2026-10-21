// 048B Parametric HUD: HUD metadata on named parameters, range-checked
// transitions, dependency inventory and a sampled scratch rebuild check.
#include "ark/parametric_hud.hpp"
#include "ark/agent.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
namespace ark {
using V=arkjson::Value;
namespace {
[[noreturn]] void invalid(const std::string& why){throw std::runtime_error("HUD_INVALID: "+why);}
double finite(const V& v,const char* key){if(v.t!=V::Num||!std::isfinite(v.num)||std::fabs(v.num)>1e8)invalid(std::string(key)+" must be a finite number of magnitude at most 1e8");return v.num;}
bool integral(double x){return std::floor(x)==x;}
void words(const std::string& text,std::set<std::string>& out){
    for(size_t i=0;i<text.size();){const unsigned char c=(unsigned char)text[i];
        if(std::isalpha(c)||c=='_'){const size_t start=i++;while(i<text.size()&&(std::isalnum((unsigned char)text[i])||text[i]=='_'))++i;out.insert(text.substr(start,i-start));}
        else if(std::isdigit(c)||c=='.'){while(i<text.size()&&(std::isalnum((unsigned char)text[i])||text[i]=='.'))++i;}else ++i;}
}
void strings(const V& v,std::set<std::string>& values){if(v.t==V::Str)values.insert(v.str);else if(v.t==V::Arr)for(const auto& x:v.arr)strings(x,values);else if(v.t==V::Obj)for(const auto& kv:v.obj)strings(kv.second,values);}
bool uses(const std::set<std::string>& texts,const std::set<std::string>& names){std::set<std::string> ids;for(const auto& t:texts)words(t,ids);return std::any_of(ids.begin(),ids.end(),[&](const std::string& id){return names.count(id)!=0;});}
std::set<std::string> featureTexts(const Feature& f){
    std::set<std::string> texts;for(const auto& kv:f.p)texts.insert(kv.second);strings(f.nativeSpec,texts);
    for(const auto& b:f.bends)for(const auto* s:{&b.feed,&b.radius,&b.angle,&b.roll})texts.insert(*s);
    for(const auto& n:f.notches){texts.insert(n.station);texts.insert(n.angle);}return texts;
}
std::set<std::string> sketchTexts(const NamedSketch& s){
    std::set<std::string> texts;for(const auto& e:s.sk.ents){for(const auto& x:e.expressions)texts.insert(x);texts.insert(e.distance);strings(e.arcRecipe,texts);strings(e.entityRecipe,texts);}
    for(const auto& c:s.sk.cons){texts.insert(c.expression);texts.insert(c.expression2);}for(const auto& c:s.sk.corners)texts.insert(c.size);return texts;
}
bool literal(const Parameter& p){bool ok=false;evalExpr(p.expr,ExprEnv{},&ok);return ok;}
const Parameter* findParam(const Document& d,const std::string& name){for(const auto& p:d.params)if(p.name==name)return &p;return nullptr;}
std::string number(double x){char text[48];std::snprintf(text,sizeof text,"%.10g",x);return text;}
std::string unitSuffix(const HudSpec& s){return s.unit=="mm"?" mm":s.unit=="deg"?" deg":"";}
void revisionCheck(const AgentSession& s,const V& args){if(const auto* e=args.find("expected_revision"))if(e->t!=V::Num||e->num!=(double)s.revision)throw std::runtime_error("Revision conflict; the current revision is in this result. Inspect only what your edit depends on and retry");}
std::vector<const Parameter*> selected(const Document& d,const V& args,size_t limit){
    std::vector<const Parameter*> out;
    if(const auto* names=args.find("names")){
        if(names->t!=V::Arr||names->arr.empty()||names->arr.size()>limit)throw std::runtime_error("HUD_INVALID: names must list 1.."+std::to_string(limit)+" parameter names");
        for(const auto& n:names->arr){if(n.t!=V::Str)throw std::runtime_error("HUD_INVALID: names must be strings");const auto* p=findParam(d,n.str);
            if(!p)throw std::runtime_error("HUD_PARAMETER_NOT_FOUND: "+n.str);if(!hudExposed(*p))throw std::runtime_error("HUD_NOT_EXPOSED: "+n.str+" has no HUD metadata");if(std::find(out.begin(),out.end(),p)==out.end())out.push_back(p);}
    }else for(const auto& p:d.params)if(hudExposed(p))out.push_back(&p);
    std::stable_sort(out.begin(),out.end(),[](const Parameter* a,const Parameter* b){const auto x=hudSpec(*a),y=hudSpec(*b);return std::tie(x.group,x.order,a->name)<std::tie(y.group,y.order,b->name);});
    return out;
}
V specJson(const Parameter& p,const HudSpec& s){
    V row=V::makeObj();row.set("name",V::makeStr(p.name));row.set("label",V::makeStr(s.label.empty()?p.name:s.label));row.set("group",V::makeStr(s.group));row.set("order",V::makeNum(s.order));row.set("unit",V::makeStr(s.unit));
    row.set("expr",V::makeStr(p.expr));row.set("value",V::makeNum(p.value));row.set("step",V::makeNum(s.step));
    if(p.hud.find("min"))row.set("min",V::makeNum(s.min));if(p.hud.find("max"))row.set("max",V::makeNum(s.max));
    const bool below=p.hud.find("min")&&p.value<s.min,above=p.hud.find("max")&&p.value>s.max;row.set("in_range",V::makeBool(!below&&!above));
    row.set("driven",V::makeBool(!literal(p)));row.set("display",V::makeStr(hudFormat(p.value,s)));return row;
}
}
arkjson::Value canonicalHud(const arkjson::Value& hud){
    if(hud.t!=V::Obj)invalid("hud must be an object or null");
    for(const auto& kv:hud.obj)if(kv.first!="label"&&kv.first!="min"&&kv.first!="max"&&kv.first!="step"&&kv.first!="unit"&&kv.first!="group"&&kv.first!="order")invalid("unknown field "+kv.first+"; use label,min,max,step,unit,group,order");
    V out=V::makeObj();std::string unit="none";
    if(const auto* u=hud.find("unit")){if(u->t!=V::Str||(u->str!="mm"&&u->str!="deg"&&u->str!="count"&&u->str!="none"))invalid("unit must be mm, deg, count or none");unit=u->str;}
    for(const char* key:{"label","group"})if(const auto* t=hud.find(key)){if(t->t!=V::Str||t->str.size()>64||(std::string(key)=="label"&&t->str.empty()))invalid(std::string(key)+" must be text of "+(std::string(key)=="label"?"1":"0")+"..64 bytes");if(!t->str.empty())out.set(key,*t);}
    const V* lo=hud.find("min");const V* hi=hud.find("max");double step=1;
    if(lo)finite(*lo,"min");if(hi)finite(*hi,"max");
    if(lo&&hi&&!(lo->num<hi->num))invalid("min must be less than max");
    if(const auto* s=hud.find("step")){step=finite(*s,"step");if(!(step>0))invalid("step must be greater than zero");}
    else if(lo&&hi&&unit!="count")step=std::pow(10.,std::floor(std::log10((hi->num-lo->num)/20)));
    if(lo&&hi&&step>hi->num-lo->num)invalid("step must not exceed max-min");
    if(unit=="count"&&(!integral(step)||(lo&&!integral(lo->num))||(hi&&!integral(hi->num))))invalid("count min, max and step must be integers");
    if(lo)out.set("min",*lo);if(hi)out.set("max",*hi);
    if(const auto* o=hud.find("order")){if(o->t!=V::Num||!std::isfinite(o->num)||!integral(o->num)||std::fabs(o->num)>1000)invalid("order must be an integer from -1000 to 1000");if(o->num!=0)out.set("order",V::makeNum(o->num));}
    out.set("step",V::makeNum(step));out.set("unit",V::makeStr(unit));
    std::sort(out.obj.begin(),out.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});return out;
}
bool hudExposed(const Parameter& p){return p.hud.t==V::Obj;}
HudSpec hudSpec(const Parameter& p){
    HudSpec s;if(p.hud.t!=V::Obj)return s;
    if(const auto* v=p.hud.find("label"))s.label=v->str;if(const auto* v=p.hud.find("group"))s.group=v->str;if(const auto* v=p.hud.find("unit"))s.unit=v->str;
    if(const auto* v=p.hud.find("min"))s.min=v->num;if(const auto* v=p.hud.find("max"))s.max=v->num;if(const auto* v=p.hud.find("step"))s.step=v->num;if(const auto* v=p.hud.find("order"))s.order=(int)v->num;
    return s;
}
void applyParameterHud(Document& next,const std::string& name,const arkjson::Value& hud){
    auto found=std::find_if(next.params.begin(),next.params.end(),[&](const Parameter& p){return p.name==name;});
    if(found==next.params.end())throw std::runtime_error("HUD_PARAMETER_NOT_FOUND: "+name+"; supply expr to create the parameter");
    found->hud=hud.isNull()?V{}:canonicalHud(hud);
}
void validateHudTransition(const Document& before,const Document& after){
    size_t count=0;for(const auto& p:after.params)count+=hudExposed(p);
    if(count>maxHudParameters)throw std::runtime_error("HUD_CAPACITY: at most "+std::to_string(maxHudParameters)+" parameters can be exposed to the HUD");
    for(const auto& p:after.params){if(!hudExposed(p))continue;const auto* old=findParam(before,p.name);
        if(old&&old->hud.dump()==p.hud.dump()&&old->value==p.value)continue; // unchanged rows keep legacy state
        const auto s=hudSpec(p);const bool lo=p.hud.find("min")!=nullptr,hi=p.hud.find("max")!=nullptr;
        if(!std::isfinite(p.value)||(lo&&p.value<s.min)||(hi&&p.value>s.max))
            throw std::runtime_error("HUD_VALUE_OUT_OF_RANGE: "+p.name+" = "+number(p.value)+unitSuffix(s)+" is outside ["+(lo?number(s.min):std::string("-inf"))+", "+(hi?number(s.max):std::string("inf"))+"]"+unitSuffix(s)+"; the document is unchanged. Choose a value inside the HUD range or change the range explicitly");
        if(s.unit=="count"&&!integral(p.value))throw std::runtime_error("HUD_VALUE_NOT_INTEGER: "+p.name+" = "+number(p.value)+" is a count; the document is unchanged");
    }
}
std::string hudFormat(double value,const HudSpec& spec){
    int decimals=0;for(double s=spec.step;decimals<6&&std::fabs(s-std::round(s))>1e-9;s*=10)++decimals;
    char text[64];std::snprintf(text,sizeof text,"%.*f",decimals,value);return std::string(text)+unitSuffix(spec);
}
double hudSnap(double value,const HudSpec& spec){
    const double origin=std::isfinite(spec.min)?spec.min:0;double snapped=origin+std::round((value-origin)/spec.step)*spec.step;
    if(std::fabs(snapped)<1e-12)snapped=0;return snapped;
}
std::vector<u64> hudDependentFeatures(const Document& d,const std::string& parameter,std::vector<std::string>* via){
    std::set<std::string> names{parameter};
    for(bool grew=true;grew;){grew=false;for(const auto& p:d.params){if(names.count(p.name))continue;std::set<std::string> ids;words(p.expr,ids);if(std::any_of(ids.begin(),ids.end(),[&](const std::string& id){return names.count(id)!=0;})){names.insert(p.name);grew=true;}}}
    std::set<std::string> planes;
    for(bool grew=true;grew;){grew=false;for(const auto& plane:d.planes){if(planes.count(plane.name))continue;std::set<std::string> texts;for(const auto& kv:plane.p)texts.insert(kv.second);if(uses(texts,names)||planes.count(plane.parent)){planes.insert(plane.name);grew=true;}}}
    std::set<std::string> sketches;for(const auto& s:d.sketches)if(uses(sketchTexts(s),names)||planes.count(s.sk.plane))sketches.insert(s.name);
    std::vector<u64> out;std::set<std::string> reasons;
    for(const auto& f:d.features){const auto texts=featureTexts(f);std::string why;
        std::set<std::string> ids;for(const auto& t:texts)words(t,ids);
        for(const auto& id:ids)if(names.count(id)){why=id==parameter?"expression":"parameter:"+id;break;}
        if(why.empty())for(const auto& t:texts){if(sketches.count(t)){why="sketch:"+t;break;}if(planes.count(t)){why="plane:"+t;break;}}
        if(!why.empty()){out.push_back(f.id);if(via)via->push_back(why);}
    }
    return out;
}
namespace {
V hudInfo(const AgentSession& s,const V& args){
    for(const auto& kv:args.obj)if(kv.first!="names"&&kv.first!="expected_revision")throw std::runtime_error("HUD_INVALID: unknown hud_info field "+kv.first);
    revisionCheck(s,args);const auto& d=s.document;const auto rows=selected(d,args,maxHudParameters);
    V out=V::makeObj(),list=V::makeArr();size_t exposed=0;for(const auto& p:d.params)exposed+=hudExposed(p);
    for(const auto* p:rows){V row=specJson(*p,hudSpec(*p)),dependents=V::makeArr();std::vector<std::string> via;const auto ids=hudDependentFeatures(d,p->name,&via);
        std::set<u64> direct(ids.begin(),ids.end()),downstream;
        for(const auto& f:d.features)if(!direct.count(f.id)&&std::any_of(f.inputs.begin(),f.inputs.end(),[&](u64 in){return direct.count(in)||downstream.count(in);}))downstream.insert(f.id);
        for(size_t i=0;i<ids.size();++i){V dep=V::makeObj();dep.set("id",V::makeNum((double)ids[i]));if(const auto* f=d.find(ids[i]))dep.set("name",V::makeStr(f->name));dep.set("via",V::makeStr(via[i]));dependents.arr.push_back(dep);}
        V after=V::makeArr();for(u64 id:downstream)after.arr.push_back(V::makeNum((double)id));
        row.set("dependents",dependents);row.set("downstream_feature_ids",after);list.arr.push_back(row);}
    out.set("revision",V::makeNum((double)s.revision));out.set("parameters",list);out.set("exposed_count",V::makeNum((double)exposed));out.set("max_parameters",V::makeNum((double)maxHudParameters));out.set("hidden_parameter_count",V::makeNum((double)(d.params.size()-exposed)));
    out.set("edit",V::makeStr("Change a value with doc_apply param.set{name,expr} and the observed expected_revision. Out-of-range values reject with HUD_VALUE_OUT_OF_RANGE and leave the document unchanged. Driven rows are expressions of other parameters: edit their drivers."));
    out.set("dependency_scope",V::makeStr("dependents are features whose authored expressions reference the parameter directly, through a dependent parameter, or through a sketch/datum whose expressions do; downstream_feature_ids consume those features as inputs and are rebuilt too."));
    return out;
}
V hudFlexCheck(AgentSession& s,const V& args){
    for(const auto& kv:args.obj)if(kv.first!="names"&&kv.first!="values"&&kv.first!="expected_revision"&&kv.first!="max_evaluations"&&kv.first!="max_seconds")throw std::runtime_error("HUD_INVALID: unknown hud_flex_check field "+kv.first);
    revisionCheck(s,args);const auto rows=selected(s.document,args,16);
    std::vector<std::string> points{"min","mid","max"};
    if(const auto* v=args.find("values")){if(v->t!=V::Arr||v->arr.empty()||v->arr.size()>3)throw std::runtime_error("HUD_INVALID: values must list 1..3 of min, mid, max");points.clear();
        for(const auto& x:v->arr){if(x.t!=V::Str||(x.str!="min"&&x.str!="mid"&&x.str!="max"))throw std::runtime_error("HUD_INVALID: values must be min, mid or max");if(std::find(points.begin(),points.end(),x.str)==points.end())points.push_back(x.str);}}
    auto bounded=[&](const char* key,double dflt,double lo,double hi){const auto* v=args.find(key);if(!v)return dflt;if(v->t!=V::Num||!std::isfinite(v->num)||v->num<lo||v->num>hi||!integral(v->num))throw std::runtime_error(std::string("HUD_INVALID: ")+key+" must be an integer from "+number(lo)+" to "+number(hi));return v->num;};
    const int budget=(int)bounded("max_evaluations",48,1,96);const double seconds=bounded("max_seconds",30,1,120);
    const auto started=std::chrono::steady_clock::now();auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();};
    V parameters=V::makeArr(),skipped=V::makeArr();int evaluations=0,passed=0,failed=0,pending=0;
    for(const auto* p:rows){const auto spec=hudSpec(*p);
        auto skip=[&](const char* why){V row=V::makeObj();row.set("name",V::makeStr(p->name));row.set("reason",V::makeStr(why));skipped.arr.push_back(row);};
        if(!literal(*p)){skip("driven: its expression depends on other parameters; sample its drivers");continue;}
        if(!p->hud.find("min")||!p->hud.find("max")){skip("unbounded: declare min and max to sample this parameter");continue;}
        V row=specJson(*p,spec),samples=V::makeArr();
        for(const auto& point:points){
            double value=point=="min"?spec.min:point=="max"?spec.max:std::clamp(hudSnap((spec.min+spec.max)/2,spec),spec.min,spec.max);
            V sample=V::makeObj();sample.set("at",V::makeStr(point));sample.set("value",V::makeNum(value));sample.set("display",V::makeStr(hudFormat(value,spec)));
            if(evaluations>=budget||elapsed()>seconds){sample.set("status",V::makeStr("not_evaluated"));sample.set("reason",V::makeStr(evaluations>=budget?"max_evaluations reached":"max_seconds reached"));++pending;samples.arr.push_back(sample);continue;}
            ++evaluations;const auto began=std::chrono::steady_clock::now();
            AgentSession candidate;candidate.document=s.document.authoringCopy();
            candidate.document.evaluationCache=s.document.evaluationCache;candidate.document.evaluationGeneration=s.document.evaluationGeneration;candidate.document.referenceFrameCache=s.document.referenceFrameCache;candidate.document.centroidCache=s.document.centroidCache;
            candidate.document.iterationHistory=V{};candidate.suppressIterationHistory();candidate.suppressFailureCapture();candidate.document.evaluatingBodies=nullptr;candidate.revision=s.revision;
            V apply=V::makeObj(),commands=V::makeArr(),command=V::makeObj();command.set("op",V::makeStr("param.set"));command.set("name",V::makeStr(p->name));command.set("expr",V::makeNum(value));commands.arr.push_back(command);
            apply.set("commands",commands);apply.set("expected_revision",V::makeNum((double)candidate.revision));apply.set("detail",V::makeStr("changes"));
            const V response=candidate.call("doc_apply",apply);const bool ok=!response.find("isError")->b;
            sample.set("ms",V::makeNum(std::round(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()*10)/10));
            sample.set("status",V::makeStr(ok?"pass":"fail"));
            if(ok)++passed;else{++failed;const V* data=response.find("structuredContent");const std::string error=data&&data->find("error")?data->find("error")->asStr():"Rebuild failed";
                sample.set("error",V::makeStr(error));if(data&&data->find("error_code"))sample.set("error_code",*data->find("error_code"));
                if(const auto at=error.find("Feature #");at!=std::string::npos){const u64 id=std::strtoull(error.c_str()+at+9,nullptr,10);V feature=V::makeObj();feature.set("id",V::makeNum((double)id));if(const auto* f=s.document.find(id))feature.set("name",V::makeStr(f->name));sample.set("failing_feature",feature);}
                else if(const auto at=error.find("Sketch '");at!=std::string::npos){const auto end=error.find('\'',at+8);if(end!=std::string::npos)sample.set("failing_sketch",V::makeStr(error.substr(at+8,end-at-8)));}}
            samples.arr.push_back(sample);
        }
        row.set("samples",samples);parameters.arr.push_back(row);
    }
    V out=V::makeObj();out.set("revision",V::makeNum((double)s.revision));out.set("parameters",parameters);out.set("skipped",skipped);
    out.set("passed",V::makeNum(passed));out.set("failed",V::makeNum(failed));out.set("not_evaluated",V::makeNum(pending));out.set("evaluations",V::makeNum(evaluations));
    out.set("all_passed",V::makeBool(failed==0&&pending==0&&passed>0));out.set("complete",V::makeBool(pending==0));out.set("elapsed_ms",V::makeNum(std::round(elapsed()*1000)));
    out.set("committed",V::makeBool(false));out.set("document_unchanged",V::makeBool(true));out.set("sampled_values_only",V::makeBool(true));
    out.set("scope",V::makeStr("Each parameter was rebuilt alone at the listed sample values, with every other parameter at its current value, on a scratch copy through the same doc_apply validation. A pass covers only that sampled value; it is not a proof for the whole range, for unsampled values or for combinations of parameters."));
    return out;
}
}
bool callParametricHudTool(AgentSession& session,const std::string& tool,const arkjson::Value& args,arkjson::Value& out){
    if(tool=="hud_info"){out=hudInfo(session,args);return true;}
    if(tool=="hud_flex_check"){out=hudFlexCheck(session,args);return true;}
    return false;
}
void appendParametricHudTools(arkjson::Value& tools){
    auto add=[&](const char* name,const char* description,const char* schema){V tool=V::makeObj(),parsed,annotations=V::makeObj();arkjson::parse(schema,parsed);tool.set("name",V::makeStr(name));tool.set("description",V::makeStr(description));tool.set("inputSchema",parsed);annotations.set("readOnlyHint",V::makeBool(true));tool.set("annotations",annotations);tools.arr.push_back(tool);};
    add("hud_info","Read-only Parametric HUD inventory (048B): parameters exposed to the Studio HUD with label, expression, evaluated value, min/max/step, unit, group/order, in_range and driven flags, plus the features whose expressions depend on each. Use the HUD only when the user explicitly asks for adjustable or parametric dimensions; agent_onboard topic hud has the protocol.",
        R"({"type":"object","properties":{"names":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"string"}},"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})");
    add("hud_flex_check","Read-only sampled rebuild check for HUD parameters (048B). Rebuilds each selected bounded literal HUD parameter alone at min, mid (snapped to step) and max on a scratch copy through doc_apply validation, never committing. Reports pass/fail per value with the failing feature and error. Covers only the sampled values; not a proof for the whole range or for combinations. names selects at most 16 parameters; max_evaluations 1..96 (default 48) and max_seconds 1..120 (default 30) bound the run and report not_evaluated samples explicitly.",
        R"({"type":"object","properties":{"names":{"type":"array","minItems":1,"maxItems":16,"items":{"type":"string"}},"values":{"type":"array","minItems":1,"maxItems":3,"items":{"type":"string","enum":["min","mid","max"]}},"max_evaluations":{"type":"integer","minimum":1,"maximum":96},"max_seconds":{"type":"integer","minimum":1,"maximum":120},"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})");
    for(auto& tool:tools.arr){const auto* name=tool.find("name");if(!name||name->asStr()!="doc_apply")continue;
        V schema=*tool.find("inputSchema"),props=*schema.find("properties"),commands=*props.find("commands"),items=*commands.find("items"),fields=*items.find("properties"),hud;
        arkjson::parse(R"({"type":["object","null"],"properties":{"label":{"type":"string","minLength":1,"maxLength":64},"min":{"type":"number"},"max":{"type":"number"},"step":{"type":"number","exclusiveMinimum":0},"unit":{"type":"string","enum":["mm","deg","count","none"]},"group":{"type":"string","maxLength":64},"order":{"type":"integer","minimum":-1000,"maximum":1000}},"additionalProperties":false,"description":"param.set only (048B Parametric HUD): expose the parameter to the human HUD; null hides it. expr may be omitted to change only the HUD of an existing parameter. Values outside min..max reject with HUD_VALUE_OUT_OF_RANGE. Use only when the user asks for a HUD or parametric design; see agent_onboard topic hud."})",hud);
        fields.set("hud",hud);items.set("properties",fields);commands.set("items",items);props.set("commands",commands);schema.set("properties",props);tool.set("inputSchema",schema);}
}
bool onboardingParametricHud(const AgentSession& session,const std::string& topic,arkjson::Value& r){
    auto list=[](std::initializer_list<const char*> lines){V rows=V::makeArr();for(const auto* line:lines)rows.arr.push_back(V::makeStr(line));return rows;};
    if(topic=="overview"){r.set("parametric_hud",V::makeStr("Only when the user explicitly asks for a HUD, adjustable dimensions or a parametric design, load agent_onboard topic hud: drive the model from named parameters with HUD ranges so the human can rebuild it from the Studio HUD. Normal designs do not need HUD metadata."));return false;}
    if(topic!="hud")return false;
    r.set("tools",list({"hud_info","hud_flex_check","doc_info","object_context","doc_preview","doc_apply","eval_expr"}));
    r.set("purpose",V::makeStr("The Parametric HUD is a Studio overlay where a human changes exposed named parameters and ARK rebuilds the model. Each change is an ordinary param.set transaction with expected_revision, atomic publication and an iteration-history actor (human from Studio, agent from doc_apply). B-rep stays the geometric authority; the HUD never edits geometry directly."));
    r.set("when",V::makeStr("Use this protocol only when the user explicitly asks for a HUD, adjustable/parametric dimensions or a configurable design. Otherwise build normally; do not add HUD metadata, ranges or flex checks to designs that did not ask for them."));
    r.set("protocol",list({
        "1. Declare the driving parameters first, each with param.set{name,expr,hud:{label,min,max,step,unit,group,order}} and a sensible range in which the design stays coherent. Keep the HUD short: the dimensions a human would really change (at most 64).",
        "2. Make every driven dimension an expression of those parameters: feature p fields, sketch dimensions, datum offsets, pattern counts. Derived dimensions are expressions too (param.set{name:inner,expr:\"length-2*wall\"}); never a literal copy of a driver value.",
        "3. Run hud_flex_check (optionally names/values) and fix every failure before reporting: adjust expressions, clearances or the declared range. Do not widen tolerances or drop solids to pass. A pass covers only the sampled min/mid/max values, one parameter at a time.",
        "4. Report the HUD parameter list (label, range, step, unit, group) from hud_info and the hud_flex_check scope: sampled values only, not a proof for the whole range or for combinations."}));
    r.set("command",V::makeStr("param.set{name,expr?,hud?}. hud:{label?:1..64 text,min?,max?,step?>0,unit?:mm|deg|count|none (default none),group?:0..64 text,order?:-1000..1000}; null removes the exposure, the parameter stays. expr may be omitted only to change the HUD of an existing parameter. Unit is display metadata only; values stay ARK units (mm, degrees for feature/plane rotations). A count range and step must be integers."));
    r.set("validation",list({
        "HUD_INVALID: unknown field, non-finite or out-of-bound number, min >= max, step <= 0 or larger than max-min, bad unit/label/group/order, non-integer count bounds.",
        "HUD_PARAMETER_NOT_FOUND: hud without expr on a missing parameter. HUD_CAPACITY: more than 64 exposed parameters.",
        "HUD_VALUE_OUT_OF_RANGE / HUD_VALUE_NOT_INTEGER: a batch leaves a new or changed HUD parameter outside its range (or a count non-integer). The batch is rejected atomically and the document stays unchanged; ARK never clamps an agent value. Unchanged legacy rows are not rechecked.",
        "A value inside the range can still fail to rebuild (Feature #id: ...). That rejection is atomic too; the previous geometry and revision stay intact."}));
    r.set("human_ui",V::makeStr("Studio Model > More > HUD (or Manage > HUD) toggles a translucent viewport overlay grouped by group/order. Each row shows label, value with unit, -/+ by step, click-to-type and the range. Edits are staged and highlighted until Rebuild applies them in one atomic doc_apply at the observed revision; Auto rebuilds each edit at once; Reset discards staged edits. The HUD clamps typed values to the range and shows the limit, reports measured rebuild time and failures, and refreshes when an agent commits param.set. The Parameters explorer row offers Show in HUD to expose an existing parameter."));
    V example;arkjson::parse(R"JSON({"commands":[{"op":"param.set","name":"length","expr":120,"hud":{"label":"Length","min":60,"max":240,"step":5,"unit":"mm","group":"Body","order":1}},{"op":"param.set","name":"width","expr":60,"hud":{"label":"Width","min":30,"max":120,"step":5,"unit":"mm","group":"Body","order":2}},{"op":"param.set","name":"height","expr":40,"hud":{"label":"Height","min":20,"max":80,"step":5,"unit":"mm","group":"Body","order":3}},{"op":"param.set","name":"hole_r","expr":"min(length,width)/8"},{"op":"feature.add","type":"native_solid","name":"Body","native":{"op":"box"},"p":{"width":"length","depth":"width","height":"height"}}]})JSON",example);
    example.set("expected_revision",V::makeNum((double)session.revision));r.set("example_doc_apply",example);
    r.set("example_scope",V::makeStr("Illustrative command shape only: three HUD drivers and one derived expression. Inspect actual feature keys with object_context before editing an existing model."));
    r.set("next_step",V::makeStr("Declare drivers with HUD ranges, author driven expressions, run hud_flex_check, fix failures, then report hud_info's list and the sampled scope."));
    return true;
}
}
