#include "ark/parametric_programs.hpp"
#include "ark/serialize.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
// One program publishes at most 126 commands so program.clear, the emitted
// source and program.bind fit doc_apply's 128-operation atomic batch.
constexpr size_t kMaxProgramBytes=256*1024,kMaxEmitted=126,kMaxSteps=50000,kMaxLoop=1024,kMaxBlockDepth=32,kMaxCallDepth=16,kMaxStatements=8192,kMaxOutputBytes=2*1024*1024;
const char* kOwned[]={"features","sketches","planes","materials"};
const V& get(const V& v,const char* key){static V null;const auto* p=v.find(key);return p?*p:null;}
[[noreturn]] void fail(const std::string& code,const std::string& why){throw std::runtime_error("PROGRAM_"+code+": "+why);}
void erase(V& v,const char* key){v.obj.erase(std::remove_if(v.obj.begin(),v.obj.end(),[&](const auto& p){return p.first==key;}),v.obj.end());}
void canonical(V& v){if(v.t==V::Obj){for(auto& p:v.obj)canonical(p.second);std::sort(v.obj.begin(),v.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(v.t==V::Arr)for(auto& p:v.arr)canonical(p);}
std::string hash(V v){canonical(v);return iterationSha256(v.dump());}
bool digest(const V& v){return v.t==V::Str&&v.str.size()==64&&v.str.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool reserved(const std::string& s){return s=="pi"||s=="e"||s=="true"||s=="false";}
bool identifier(const std::string& s,size_t limit=64){if(s.empty()||s.size()>limit||!(std::isalpha((unsigned char)s[0])||s[0]=='_'))return false;return s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==std::string::npos&&!reserved(s);}
void nameSpace(const std::string& s){if(s.size()>32||!identifier(s,32))fail("NAMESPACE","Use an identifier of 1..32 ASCII letters, digits or underscores");}
std::string text(const V& v,const char* key,size_t limit=128){const auto& p=get(v,key);if(p.t!=V::Str||p.str.empty()||p.str.size()>limit)fail("TEXT",std::string(key)+" needs nonempty text of at most "+std::to_string(limit)+" bytes");return p.str;}
void only(const V& v,std::initializer_list<const char*> keys,const std::string& at){for(const auto& kv:v.obj)if(std::none_of(keys.begin(),keys.end(),[&](const char* k){return kv.first==k;}))fail("SYNTAX",at+": unknown field "+kv.first);}
// Shortest round-trip decimal; integers print without exponent.
std::string number(double x){if(x==0)x=0;char buf[40];if(x==std::floor(x)&&std::fabs(x)<1e15){std::snprintf(buf,sizeof buf,"%lld",(long long)x);return buf;}for(int precision=1;precision<=17;++precision){std::snprintf(buf,sizeof buf,"%.*g",precision,x);if(std::strtod(buf,nullptr)==x)break;}return buf;}
std::string prefixed(const std::string& collection,const std::string& name,const std::string& ns){return collection=="features"?ns+" / "+name:ns+"__"+name;}
bool namespaced(const std::string& collection,const std::string& name,const std::string& ns){return name.rfind(collection=="features"?ns+" / ":ns+"__",0)==0;}

// Program scopes hold numbers only. Frame 0 holds the immutable parameters.
struct Scope{
    std::vector<std::map<std::string,double>> frames;
    const double* find(const std::string& n) const{for(auto it=frames.rbegin();it!=frames.rend();++it){auto f=it->find(n);if(f!=it->end())return &f->second;}return nullptr;}
};

// Compile-time arithmetic, comparisons, logic and a ternary over program
// numbers. A null scope only checks syntax: identifiers read as 1 and
// non-finite intermediate values are allowed.
class Expr{
    const std::string& s;const Scope* scope;const std::string& at;size_t i=0;unsigned depth=0;
    [[noreturn]] void bad(const std::string& why) const{fail("EXPRESSION",at+": "+why+" in '"+s+"'");}
    void ws(){while(i<s.size()&&std::isspace((unsigned char)s[i]))++i;}
    bool peek(const char* t){ws();return s.compare(i,std::strlen(t),t)==0;}
    bool eat(const char* t){if(!peek(t))return false;i+=std::strlen(t);return true;}
    double ternary(){if(++depth>64)bad("nesting deeper than 64");const double c=logicalOr();double v=c;if(eat("?")){const double a=ternary();if(!eat(":"))bad("expected ':'");const double b=ternary();v=c!=0?a:b;}--depth;return v;}
    double logicalOr(){double v=logicalAnd();while(eat("||")){const double r=logicalAnd();v=(v!=0||r!=0)?1:0;}return v;}
    double logicalAnd(){double v=compare();while(eat("&&")){const double r=compare();v=(v!=0&&r!=0)?1:0;}return v;}
    double compare(){
        const double a=additive();
        static const char* ops[]={"<=",">=","==","!=","<",">"};
        for(const char* op:ops)if(eat(op)){const double b=additive();for(const char* again:ops)if(peek(again))bad("chain comparisons with &&");
            const std::string o=op;return (o=="<="?a<=b:o==">="?a>=b:o=="=="?a==b:o=="!="?a!=b:o=="<"?a<b:a>b)?1:0;}
        if(peek("=")&&!peek("=="))bad("use == for equality");return a;
    }
    double additive(){double v=multiplicative();for(;;){ws();if(i<s.size()&&s[i]=='+'){++i;v+=multiplicative();}else if(i<s.size()&&s[i]=='-'){++i;v-=multiplicative();}else return v;}}
    double multiplicative(){double v=unary();for(;;){ws();if(i>=s.size())return v;const char c=s[i];if(c=='*'){++i;v*=unary();}else if(c=='/'){++i;v/=unary();}else if(c=='%'){++i;const double d=unary();v=v-d*std::floor(v/d);}else return v;}}
    double unary(){ws();if(i<s.size()&&s[i]=='-'){++i;return -unary();}if(i<s.size()&&s[i]=='+'){++i;return unary();}if(i<s.size()&&s[i]=='!'&&!(i+1<s.size()&&s[i+1]=='=')){++i;return unary()==0?1:0;}return power();}
    double power(){const double b=atom();if(eat("^"))return std::pow(b,unary());return b;}
    double atom(){
        ws();if(i>=s.size())bad("unexpected end");
        if(s[i]=='('){++i;const double v=ternary();if(!eat(")"))bad("expected ')'");return v;}
        if(std::isdigit((unsigned char)s[i])||s[i]=='.'){
            const size_t start=i;bool digits=false;while(i<s.size()&&std::isdigit((unsigned char)s[i])){++i;digits=true;}
            if(i<s.size()&&s[i]=='.'){++i;while(i<s.size()&&std::isdigit((unsigned char)s[i])){++i;digits=true;}}
            if(!digits)bad("invalid number");
            if(i<s.size()&&(s[i]=='e'||s[i]=='E')){size_t j=i+1;if(j<s.size()&&(s[j]=='+'||s[j]=='-'))++j;if(j<s.size()&&std::isdigit((unsigned char)s[j])){i=j;while(i<s.size()&&std::isdigit((unsigned char)s[i]))++i;}}
            return std::strtod(s.substr(start,i-start).c_str(),nullptr);
        }
        if(!(std::isalpha((unsigned char)s[i])||s[i]=='_'))bad("unexpected '"+s.substr(i,1)+"'");
        const size_t start=i;while(i<s.size()&&(std::isalnum((unsigned char)s[i])||s[i]=='_'))++i;const auto id=s.substr(start,i-start);
        if(eat("(")){std::vector<double> args;if(!eat(")")){do args.push_back(ternary());while(eat(","));if(!eat(")"))bad("expected ')' after arguments of "+id);}return call(id,args);}
        if(id=="pi")return 3.14159265358979323846;if(id=="e")return 2.71828182845904523536;if(id=="true")return 1;if(id=="false")return 0;
        if(!scope)return 1;if(const double* v=scope->find(id))return *v;bad("unknown identifier '"+id+"'");
    }
    double call(const std::string& f,const std::vector<double>& a){
        auto arity=[&](size_t lo,size_t hi){if(a.size()<lo||a.size()>hi)bad(f+" expects "+(lo==hi?std::to_string(lo):std::to_string(lo)+(hi>64?"+":".."+std::to_string(hi)))+" argument(s)");};
        static const std::map<std::string,double(*)(double)> one{
            {"sin",[](double x){return std::sin(x);}},{"cos",[](double x){return std::cos(x);}},{"tan",[](double x){return std::tan(x);}},
            {"asin",[](double x){return std::asin(x);}},{"acos",[](double x){return std::acos(x);}},{"atan",[](double x){return std::atan(x);}},
            {"sqrt",[](double x){return std::sqrt(x);}},{"abs",[](double x){return std::fabs(x);}},{"floor",[](double x){return std::floor(x);}},
            {"ceil",[](double x){return std::ceil(x);}},{"round",[](double x){return std::round(x);}},{"trunc",[](double x){return std::trunc(x);}},
            {"sign",[](double x){return (double)((x>0)-(x<0));}},{"exp",[](double x){return std::exp(x);}},{"log",[](double x){return std::log(x);}},
            {"log10",[](double x){return std::log10(x);}},{"deg",[](double x){return x*3.14159265358979323846/180;}}};
        if(auto u=one.find(f);u!=one.end()){arity(1,1);return u->second(a[0]);}
        if(f=="atan2"){arity(2,2);return std::atan2(a[0],a[1]);}
        if(f=="pow"){arity(2,2);return std::pow(a[0],a[1]);}
        if(f=="hypot"){arity(2,2);return std::hypot(a[0],a[1]);}
        if(f=="mod"){arity(2,2);return a[0]-a[1]*std::floor(a[0]/a[1]);}
        if(f=="min"||f=="max"){arity(1,256);double v=a[0];for(double x:a)v=f=="min"?std::min(v,x):std::max(v,x);return v;}
        if(f=="clamp"){arity(3,3);return std::min(std::max(a[0],a[1]),a[2]);}
        if(f=="lerp"){arity(3,3);return a[0]+(a[1]-a[0])*a[2];}
        bad("unknown function '"+f+"'");
    }
public:
    Expr(const std::string& source,const Scope* values,const std::string& where):s(source),scope(values),at(where){}
    double run(){if(s.empty()||s.size()>2048)bad("expressions need 1..2048 bytes");const double v=ternary();ws();if(i!=s.size())bad("unexpected '"+s.substr(i,1)+"'");if(scope&&!std::isfinite(v))fail("NONFINITE",at+": '"+s+"' is not a finite number");return v;}
};

// "{expr}" alone becomes a JSON number; mixed text interpolates the shortest
// round-trip value. "{{" and "}}" are literal braces. Other strings pass
// unchanged, so document parameter expressions keep their native meaning.
// Driven sketch coordinates always stay text: a numeric sketch coordinate is a
// free solver variable, while text is driven exactly.
V templated(const std::string& source,const Scope* scope,const std::string& at,bool driven=false){
    if(source.find_first_of("{}")==std::string::npos)return V::makeStr(source);
    std::string out;size_t placeholders=0;bool literal=false;double single=0;
    for(size_t i=0;i<source.size();){
        if(source.compare(i,2,"{{")==0){out+='{';i+=2;literal=true;continue;}
        if(source.compare(i,2,"}}")==0){out+='}';i+=2;literal=true;continue;}
        if(source[i]=='}')fail("TEMPLATE",at+": unmatched '}' in '"+source+"'");
        if(source[i]!='{'){out+=source[i++];literal=true;continue;}
        const auto end=source.find('}',i+1);if(end==std::string::npos)fail("TEMPLATE",at+": unclosed '{' in '"+source+"'");
        single=Expr(source.substr(i+1,end-i-1),scope,at).run();++placeholders;out+=number(single);i=end+1;
    }
    return placeholders==1&&!literal&&!driven?V::makeNum(single):V::makeStr(out);
}
enum class Emit{Plain,Sketch,Driven};
Emit childMode(Emit mode,const std::string& key){static const std::set<std::string> coordinates{"dof","start","to","c1","c2","radius","val","val2"};return mode==Emit::Sketch&&coordinates.count(key)?Emit::Driven:mode;}

const char* statementKind(const V& st,const std::string& at){
    if(st.t!=V::Obj)fail("SYNTAX",at+" must be a statement object");
    static const char* kinds[]={"let","set","for","if","emit","assert","call"};const char* kind=nullptr;
    for(const char* k:kinds)if(st.find(k)){if(kind)fail("SYNTAX",at+" mixes "+kind+" and "+k);kind=k;}
    if(!kind)fail("SYNTAX",at+" needs one of let, set, for, if, emit, assert or call");return kind;
}
void checkValue(const V& v,const std::string& at){if(v.t==V::Num){if(!std::isfinite(v.num))fail("NONFINITE",at);return;}if(v.t!=V::Str)fail("SYNTAX",at+" needs a number or expression text");Expr(v.str,nullptr,at).run();}
void checkTemplate(const V& v,const std::string& at,unsigned depth){
    if(depth>64)fail("SYNTAX",at+": emitted value nesting deeper than 64");
    if(v.t==V::Str)templated(v.str,nullptr,at);
    else if(v.t==V::Arr){for(size_t k=0;k<v.arr.size();++k){const auto& item=v.arr[k];const auto where=at+"["+std::to_string(k)+"]";
        if(item.t==V::Obj&&item.find("$for")){only(item,{"$for","count","item"},where);if(get(item,"$for").t!=V::Str||!identifier(get(item,"$for").str))fail("SYNTAX",where+".$for needs an identifier");checkValue(get(item,"count"),where+".count");if(!item.find("item"))fail("SYNTAX",where+" needs item");checkTemplate(get(item,"item"),where+".item",depth+1);}
        else checkTemplate(item,where,depth+1);}}
    else if(v.t==V::Obj)for(const auto& kv:v.obj){if(!kv.first.empty()&&kv.first[0]=='$')fail("SYNTAX",at+": unknown directive "+kv.first);checkTemplate(kv.second,at+"."+kv.first,depth+1);}
}
void checkBlock(const V& body,const V& functions,const std::set<std::string>& parameters,const std::string& at,unsigned depth,size_t& total){
    if(body.t!=V::Arr)fail("SYNTAX",at+" must be an array of statements");if(depth>kMaxBlockDepth)fail("SYNTAX",at+": blocks nest deeper than "+std::to_string(kMaxBlockDepth));
    for(size_t k=0;k<body.arr.size();++k){
        const auto& st=body.arr[k];const auto where=at+"["+std::to_string(k)+"]";const std::string kind=statementKind(st,where);
        if(++total>kMaxStatements)fail("CAPACITY","Programs hold at most "+std::to_string(kMaxStatements)+" statements");
        if(kind=="let"||kind=="set"){only(st,{kind.c_str(),"value"},where);const auto& name=get(st,kind.c_str());if(name.t!=V::Str||!identifier(name.str))fail("SYNTAX",where+"."+kind+" needs an identifier");if(parameters.count(name.str))fail("SYNTAX",where+": parameter "+name.str+" is immutable");checkValue(get(st,"value"),where+".value");}
        else if(kind=="for"){only(st,{"for","count","body"},where);const auto& name=get(st,"for");if(name.t!=V::Str||!identifier(name.str)||parameters.count(name.str))fail("SYNTAX",where+".for needs an identifier that is not a parameter");checkValue(get(st,"count"),where+".count");checkBlock(get(st,"body"),functions,parameters,where+".body",depth+1,total);}
        else if(kind=="if"){only(st,{"if","then","else"},where);checkValue(get(st,"if"),where+".if");checkBlock(get(st,"then"),functions,parameters,where+".then",depth+1,total);if(st.find("else"))checkBlock(get(st,"else"),functions,parameters,where+".else",depth+1,total);}
        else if(kind=="assert"){only(st,{"assert","message"},where);checkValue(get(st,"assert"),where+".assert");if(st.find("message"))text(st,"message",256);}
        else if(kind=="emit"){only(st,{"emit"},where);const auto& c=get(st,"emit");if(c.t!=V::Obj||get(c,"op").t!=V::Str)fail("SYNTAX",where+".emit needs a command object with op");checkTemplate(c,where+".emit",0);}
        else{only(st,{"call","args"},where);const auto& name=get(st,"call");const auto* fn=name.t==V::Str?functions.find(name.str.c_str()):nullptr;if(!fn)fail("SYNTAX",where+": unknown function "+name.asStr());
            const auto& args=get(st,"args");if(!args.isNull()&&args.t!=V::Obj)fail("SYNTAX",where+".args must be an object");std::set<std::string> expected,given;for(const auto& p:get(*fn,"params").arr)expected.insert(p.str);for(const auto& kv:args.obj){given.insert(kv.first);checkValue(kv.second,where+".args."+kv.first);}
            if(expected!=given)fail("SYNTAX",where+": call "+name.str+" must pass exactly its declared params");}
    }
}
void validateProgram(const V& p){
    if(p.t!=V::Obj||get(p,"format").asStr()!="ark.parametric-program.v1")fail("FORMAT","Expected an object with format ark.parametric-program.v1");
    if(p.dump().size()>kMaxProgramBytes)fail("CAPACITY","Program source exceeds 256 KiB");
    only(p,{"format","name","description","parameters","functions","body"},"program");text(p,"name");if(p.find("description"))text(p,"description",2048);
    const auto& parameters=get(p,"parameters");if(!parameters.isNull()&&(parameters.t!=V::Obj||parameters.obj.size()>64))fail("PARAMETERS","parameters must be an object of at most 64 entries");
    std::set<std::string> names;
    for(const auto& kv:parameters.obj){const auto at="parameters."+kv.first;if(!identifier(kv.first))fail("PARAMETERS",at+": names are identifiers");const auto& spec=kv.second;if(spec.t!=V::Obj)fail("PARAMETERS",at+" must be an object");only(spec,{"default","min","max","integer","unit","description"},at);
        for(const char* key:{"default","min","max"})if(spec.find(key)&&(get(spec,key).t!=V::Num||!std::isfinite(get(spec,key).num)))fail("PARAMETERS",at+"."+key+" must be a finite number");
        if(!spec.find("default"))fail("PARAMETERS",at+" needs a default");if(spec.find("integer")&&get(spec,"integer").t!=V::Bool)fail("PARAMETERS",at+".integer must be boolean");
        if(spec.find("unit"))text(spec,"unit",16);if(spec.find("description"))text(spec,"description",512);
        if(spec.find("min")&&spec.find("max")&&get(spec,"min").num>get(spec,"max").num)fail("PARAMETERS",at+": min exceeds max");names.insert(kv.first);}
    const auto& functions=get(p,"functions");if(!functions.isNull()&&(functions.t!=V::Obj||functions.obj.size()>64))fail("FUNCTIONS","functions must be an object of at most 64 entries");
    size_t total=0;
    for(const auto& kv:functions.obj){const auto at="functions."+kv.first;if(!identifier(kv.first))fail("FUNCTIONS",at+": names are identifiers");const auto& fn=kv.second;if(fn.t!=V::Obj)fail("FUNCTIONS",at+" must be an object");only(fn,{"params","body","description"},at);
        const auto& params=get(fn,"params");if(!params.isNull()&&(params.t!=V::Arr||params.arr.size()>16))fail("FUNCTIONS",at+".params must list at most 16 identifiers");std::set<std::string> seen;
        for(const auto& name:params.arr)if(name.t!=V::Str||!identifier(name.str)||names.count(name.str)||!seen.insert(name.str).second)fail("FUNCTIONS",at+".params must be distinct identifiers that are not parameters");
        if(fn.find("description"))text(fn,"description",512);checkBlock(get(fn,"body"),functions,names,at+".body",1,total);}
    checkBlock(get(p,"body"),functions,names,"body",1,total);if(get(p,"body").arr.empty())fail("SYNTAX","body needs at least one statement");
}
// Complete, validated public values: declared parameters only, finite,
// within their bounds and integral where requested.
V resolveValues(const V& program,const V& values){
    if(!values.isNull()&&values.t!=V::Obj)fail("VALUES","values must map parameter names to numbers");
    for(const auto& kv:values.obj)if(!get(program,"parameters").find(kv.first.c_str()))fail("VALUES","Undeclared parameter "+kv.first);
    V out=V::makeObj();
    for(const auto& kv:get(program,"parameters").obj){const auto& spec=kv.second;const V* given=values.find(kv.first.c_str());const V& v=given?*given:get(spec,"default");
        if(v.t!=V::Num||!std::isfinite(v.num))fail("VALUES",kv.first+" needs a finite number");
        if(spec.find("min")&&v.num<get(spec,"min").num)fail("VALUES",kv.first+"="+number(v.num)+" is below min "+number(get(spec,"min").num));
        if(spec.find("max")&&v.num>get(spec,"max").num)fail("VALUES",kv.first+"="+number(v.num)+" exceeds max "+number(get(spec,"max").num));
        if(get(spec,"integer").b&&v.num!=std::floor(v.num))fail("VALUES",kv.first+" must be an integer");out.set(kv.first,v);}
    canonical(out);return out;
}

struct Compiler{
    const V& program;Scope scope;std::set<std::string> parameters;size_t steps=0,bytes=0;unsigned calls=0;
    std::vector<V> emitted;V assertions=V::makeArr();
    explicit Compiler(const V& p):program(p){}
    void step(const std::string& at){if(++steps>kMaxSteps)fail("STEPS",at+": program exceeds "+std::to_string(kMaxSteps)+" evaluation steps");}
    double value(const V& v,const std::string& at){if(v.t==V::Num)return v.num;return Expr(v.str,&scope,at).run();}
    size_t count(const V& v,const std::string& at){const double n=value(v,at);if(n!=std::floor(n)||n<0||n>(double)kMaxLoop)fail("LOOP",at+" must be an integer count 0.."+std::to_string(kMaxLoop)+", got "+number(n));return (size_t)n;}
    V expand(const V& v,const std::string& at,Emit mode){
        if(v.t==V::Str)return templated(v.str,&scope,at,mode==Emit::Driven);
        if(v.t==V::Arr){V out=V::makeArr();for(size_t k=0;k<v.arr.size();++k){const auto& item=v.arr[k];const auto where=at+"["+std::to_string(k)+"]";
            if(item.t==V::Obj&&item.find("$for")){const auto n=count(get(item,"count"),where+".count");for(size_t j=0;j<n;++j){step(where);scope.frames.push_back({{get(item,"$for").str,(double)j}});out.arr.push_back(expand(get(item,"item"),where+".item",mode));scope.frames.pop_back();}}
            else out.arr.push_back(expand(item,where,mode));}return out;}
        if(v.t==V::Obj){V out=V::makeObj();for(const auto& kv:v.obj)out.set(kv.first,expand(kv.second,at+"."+kv.first,childMode(mode,kv.first)));return out;}
        return v;
    }
    V command(const V& source,const std::string& at){V out=V::makeObj();const bool sketch=get(source,"op").asStr()=="sketch.set";for(const auto& kv:source.obj)out.set(kv.first,expand(kv.second,at+"."+kv.first,sketch&&kv.first=="sketch"?Emit::Sketch:Emit::Plain));return out;}
    void block(const V& body,const std::string& at){scope.frames.emplace_back();for(size_t k=0;k<body.arr.size();++k)statement(body.arr[k],at+"["+std::to_string(k)+"]");scope.frames.pop_back();}
    void statement(const V& st,const std::string& at){
        step(at);const std::string kind=statementKind(st,at);
        if(kind=="let")scope.frames.back()[get(st,"let").str]=value(get(st,"value"),at+".value");
        else if(kind=="set"){const auto& name=get(st,"set").str;double* slot=nullptr;for(size_t f=scope.frames.size();f-->1&&!slot;){auto it=scope.frames[f].find(name);if(it!=scope.frames[f].end())slot=&it->second;}if(!slot)fail("SET",at+": "+name+" has no earlier let in scope");*slot=value(get(st,"value"),at+".value");}
        else if(kind=="for"){const auto n=count(get(st,"count"),at+".count");for(size_t j=0;j<n;++j){scope.frames.push_back({{get(st,"for").str,(double)j}});block(get(st,"body"),at+".body");scope.frames.pop_back();}}
        else if(kind=="if"){if(value(get(st,"if"),at+".if")!=0)block(get(st,"then"),at+".then");else if(st.find("else"))block(get(st,"else"),at+".else");}
        else if(kind=="assert"){const auto& condition=get(st,"assert");const auto message=st.find("message")?get(st,"message").str:std::string("Design constraint failed");
            if(value(condition,at+".assert")==0)fail("ASSERT",message+" ("+(condition.t==V::Str?condition.str:number(condition.num))+" at "+at+")");
            V row=V::makeObj();row.set("at",V::makeStr(at));row.set("message",V::makeStr(message));if(assertions.arr.size()<256)assertions.arr.push_back(row);}
        else if(kind=="emit"){if(emitted.size()>=kMaxEmitted)fail("CAPACITY","A program emits at most "+std::to_string(kMaxEmitted)+" commands; split it into namespaces");
            V out=command(get(st,"emit"),at+".emit");bytes+=out.dump().size();if(bytes>kMaxOutputBytes)fail("CAPACITY","Emitted source exceeds 2 MiB");emitted.push_back(std::move(out));}
        else{if(++calls>kMaxCallDepth)fail("RECURSION",at+": calls nest deeper than "+std::to_string(kMaxCallDepth));
            const auto& fn=get(get(program,"functions"),get(st,"call").str.c_str());std::map<std::string,double> args;
            for(const auto& kv:get(st,"args").obj)args[kv.first]=value(kv.second,at+".args."+kv.first);
            // Functions see only parameters and their own arguments.
            Scope caller=std::move(scope);scope=Scope{};scope.frames.push_back(caller.frames.front());scope.frames.push_back(std::move(args));
            try{block(get(fn,"body"),"functions."+get(st,"call").str+".body");}catch(...){scope=std::move(caller);throw;}
            scope=std::move(caller);--calls;}
    }
};

// Program-local names become namespaced, and references to them are rewritten
// by field meaning. Other strings name existing project objects unchanged.
using Locals=std::map<std::string,std::set<std::string>>;
const std::map<std::string,std::vector<const char*>>& referenceKeys(){static const std::map<std::string,std::vector<const char*>> keys{
    {"inputs",{"features"}},{"feature",{"features"}},{"source_feature",{"features"}},{"second_feature",{"features"}},{"alternate_feature",{"features"}},
    {"curve",{"sketches","features"}},{"path",{"sketches","features"}},{"sections",{"sketches","features"}},{"section",{"sketches","features"}},{"guides",{"sketches","features"}},{"profile",{"sketches","features"}},
    {"sketch",{"sketches"}},{"source_sketch",{"sketches"}},{"plane",{"planes"}},{"datum",{"planes"}},{"parent",{"planes"}},{"first",{"planes"}},{"second",{"planes"}},{"material",{"materials"}}};return keys;}
bool featureKey(const std::string& key){return key=="inputs"||key=="feature"||key=="source_feature"||key=="second_feature"||key=="alternate_feature";}
void rewrite(V& v,const std::string& key,bool datumKeys,const Locals& locals,const std::string& ns,const std::string& at){
    if(v.t==V::Str){const auto& keys=referenceKeys();const auto found=keys.find(key);if(found==keys.end()||(!datumKeys&&(key=="parent"||key=="first"||key=="second")))return;
        for(const char* c:found->second){const auto l=locals.find(c);if(l!=locals.end()&&l->second.count(v.str)){v.str=prefixed(c,v.str,ns);return;}}}
    else if(v.t==V::Num&&featureKey(key))fail("REFERENCE",at+": "+key+" must name a feature; numeric IDs are not stable program identities");
    else if(v.t==V::Arr)for(auto& item:v.arr)rewrite(item,key,datumKeys,locals,ns,at);
    else if(v.t==V::Obj)for(auto& kv:v.obj)rewrite(kv.second,kv.first,datumKeys,locals,ns,at+"."+kv.first);
}
const char* collectionFor(const std::string& op){return op=="feature.add"?"features":op=="sketch.set"?"sketches":op=="plane.set"?"planes":op=="material.set"?"materials":nullptr;}

V authored(const Document& d){V out;if(!arkjson::parse(documentToJson(d,false,false),out))fail("SOURCE","Cannot serialize authored source");return out;}
V fingerprints(const V& root,const V& owned){
    V out=V::makeObj();
    for(const char* c:kOwned){V values=V::makeObj();for(const auto& n:get(owned,c).arr){const V* row=nullptr;for(const auto& r:get(root,c).arr)if(get(r,"name").asStr()==n.str){if(row)fail("OWNERSHIP","Program-owned name is not unique: "+n.str);row=&r;}
        if(!row)fail("LOCAL_EDITS","A program-owned object is missing: "+std::string(c)+":"+n.str);values.set(n.str,V::makeStr(hash(*row)));}out.set(c,values);}
    return out;
}
size_t entryIndex(const Document& d,const std::string& ns){for(size_t i=0;i<d.parametricPrograms.arr.size();++i)if(get(d.parametricPrograms.arr[i],"namespace").asStr()==ns)return i;return d.parametricPrograms.arr.size();}
// A new namespace must not overlap any object or component that uses its prefix.
void namespaceFree(const Document& d,const V& root,const std::string& ns,const V* owned){
    for(const auto& e:d.parametricComponents.arr)if(get(e,"namespace").asStr()==ns)fail("NAMESPACE","A parametric component already uses namespace "+ns);
    for(const char* c:{"features","sketches","planes","materials","params"})for(const auto& row:get(root,c).arr){const auto name=get(row,"name").asStr();if(!namespaced(std::string(c)=="params"?"params":c,name,ns))continue;
        bool mine=false;if(owned)for(const auto& n:get(*owned,c).arr)mine|=n.str==name;if(!mine)fail("NAMESPACE","Existing object "+std::string(c)+":"+name+" already uses the program namespace "+ns);}
}
// Rejects removal while unrelated source still names or numbers owned objects.
void requireNoDependants(const Document& d,const V& root,const V& owned){
    std::set<std::string> names;for(const char* c:kOwned)for(const auto& n:get(owned,c).arr)names.insert(n.str);
    std::set<double> ids;for(const auto& f:d.features)if(names.count(f.name))ids.insert((double)f.id);
    std::function<void(const V&,const std::string&,const std::string&)> scan=[&](const V& v,const std::string& key,const std::string& where){
        if(v.t==V::Str&&names.count(v.str))fail("DEPENDANTS",where+" references program-owned "+v.str+"; remove or retarget it, or detach the program");
        if(v.t==V::Num&&ids.count(v.num)&&(featureKey(key)||key=="owner_feature"||key=="curve"))fail("DEPENDANTS",where+" references program-owned feature #"+number(v.num)+"; remove or retarget it, or detach the program");
        if(v.t==V::Arr)for(const auto& item:v.arr)scan(item,key,where);
        if(v.t==V::Obj)for(const auto& kv:v.obj)scan(kv.second,kv.first,where);
    };
    // Skeleton source is text; a quoted program-owned name in it is a dependency.
    if(const auto* skeleton=root.find("skeleton")){const auto source=skeleton->dump();for(const auto& n:names)if(source.find(n)!=std::string::npos)fail("DEPENDANTS","Skeleton source references program-owned "+n+"; edit the Skeleton first, or detach the program");}
    for(const auto& kv:root.obj){if(kv.first=="parametric_programs"||kv.first=="skeleton")continue;
        const bool rows=std::any_of(std::begin(kOwned),std::end(kOwned),[&](const char* c){return kv.first==c;});
        if(rows&&kv.second.t==V::Arr){for(const auto& row:kv.second.arr)if(!names.count(get(row,"name").asStr()))scan(row,"",kv.first+":"+get(row,"name").asStr());}
        else scan(kv.second,kv.first,kv.first);}
}
void removeOwned(Document& d,const V& owned){
    const auto& features=get(owned,"features").arr;
    for(auto it=features.rbegin();it!=features.rend();++it){auto f=std::find_if(d.features.begin(),d.features.end(),[&](const Feature& x){return x.name==it->str;});if(f==d.features.end()||!d.remove(f->id))fail("LOCAL_EDITS","Program-owned feature is missing: "+it->str);}
    for(const auto& n:get(owned,"sketches").arr)if(!d.removeSketch(n.str))fail("LOCAL_EDITS","Program-owned sketch is missing: "+n.str);
    for(const auto& n:get(owned,"planes").arr){auto it=std::find_if(d.planes.begin(),d.planes.end(),[&](const DatumPlane& p){return p.name==n.str;});if(it==d.planes.end())fail("LOCAL_EDITS","Program-owned datum is missing: "+n.str);d.planes.erase(it);}
    for(const auto& n:get(owned,"materials").arr){auto it=std::find_if(d.materials.begin(),d.materials.end(),[&](const Material& m){return m.name==n.str;});if(it==d.materials.end())fail("LOCAL_EDITS","Program-owned material is missing: "+n.str);d.materials.erase(it);}
}
bool localEdits(const V& root,const V& entry){try{return hash(fingerprints(root,get(entry,"owned")))!=hash(get(entry,"baseline"));}catch(const std::exception&){return true;}}
void expected(const AgentSession& s,const V& args){const auto& n=get(args,"expected_revision");if(n.t!=V::Num||n.num!=(double)s.revision)fail("REVISION_CONFLICT","Read the current document revision before preparing a program run");}
V command(const char* op,const std::string& ns){V c=V::makeObj();c.set("op",V::makeStr(op));c.set("namespace",V::makeStr(ns));return c;}
}

std::string parametricProgramHash(const V& program){return hash(program);}

V compileParametricProgram(const V& program,const V& values,const std::string& ns){
    nameSpace(ns);validateProgram(program);const V resolved=resolveValues(program,values);
    Compiler c(program);std::map<std::string,double> params;for(const auto& kv:resolved.obj){params[kv.first]=kv.second.num;c.parameters.insert(kv.first);}c.scope.frames.push_back(std::move(params));
    c.block(get(program,"body"),"body");if(c.emitted.empty())fail("EMPTY","The program emitted no commands for these values");
    Locals locals;V owned=V::makeObj();for(const char* col:kOwned)owned.set(col,V::makeArr());
    for(size_t k=0;k<c.emitted.size();++k){const auto& cmd=c.emitted[k];const auto at="emit#"+std::to_string(k+1);const auto op=get(cmd,"op").asStr();const char* col=collectionFor(op);
        if(!col)fail("OPERATION",at+": "+op+" is outside the program contract; programs emit feature.add, sketch.set, plane.set and material.set");
        const auto& name=get(cmd,"name");if(name.t!=V::Str||name.str.empty()||name.str.size()>96)fail("NAME",at+" needs a local name of 1..96 bytes");
        if(cmd.find("id"))fail("OPERATION",at+": programs create objects; id is not accepted");if(op=="sketch.set"&&cmd.find("owner_feature"))fail("OPERATION",at+": scoped owner_feature sketches are outside program ownership");
        if(!locals[col].insert(name.str).second)fail("DUPLICATE_NAME",at+": "+std::string(col)+" "+name.str+" is emitted twice");
        if(prefixed(col,name.str,ns).size()>128)fail("NAME",at+": namespaced name exceeds 128 bytes");}
    static const std::set<std::string> opaque{"op","type","appearance","part","classification","mesh","mesh_edit"};
    V commands=V::makeArr();
    for(size_t k=0;k<c.emitted.size();++k){const auto& cmd=c.emitted[k];const auto op=get(cmd,"op").str;const char* col=collectionFor(op);V out=V::makeObj();
        for(const auto& kv:cmd.obj){V value=kv.second;if(kv.first=="name")value=V::makeStr(prefixed(col,value.str,ns));else if(!opaque.count(kv.first))rewrite(value,kv.first,op=="plane.set",locals,ns,"emit#"+std::to_string(k+1)+"."+kv.first);out.set(kv.first,value);}
        auto list=get(owned,col);list.arr.push_back(get(out,"name"));owned.set(col,list);commands.arr.push_back(out);}
    V r=V::makeObj();r.set("format",V::makeStr("ark.parametric-program-compilation.v1"));r.set("name",get(program,"name"));r.set("namespace",V::makeStr(ns));r.set("program_sha256",V::makeStr(hash(program)));
    r.set("values",resolved);r.set("parameters",get(program,"parameters").isNull()?V::makeObj():get(program,"parameters"));r.set("commands",commands);r.set("owned",owned);r.set("assertions",c.assertions);r.set("steps",V::makeNum((double)c.steps));
    r.set("geometry_authority",V::makeStr("none until doc_preview/doc_apply evaluates the emitted native commands"));return r;
}

void validateParametricPrograms(const V& entries){
    if(entries.isNull())return;if(entries.t!=V::Arr||entries.arr.empty()||entries.arr.size()>64)fail("BINDINGS","Expected 1..64 program bindings");std::set<std::string> namespaces,claimed;
    for(const auto& e:entries.arr){
        if(e.t!=V::Obj)fail("BINDINGS","Binding must be an object");only(e,{"namespace","name","program","program_sha256","values","owned","baseline"},"binding");
        const auto ns=text(e,"namespace");nameSpace(ns);if(!namespaces.insert(ns).second)fail("BINDINGS","Duplicate namespace "+ns);
        const auto& program=get(e,"program");validateProgram(program);if(get(e,"program_sha256").asStr()!=hash(program))fail("HASH","Embedded program digest does not match");
        if(get(e,"name").asStr()!=get(program,"name").asStr())fail("BINDINGS","Binding name must match its program");
        if(hash(resolveValues(program,get(e,"values")))!=hash(get(e,"values")))fail("VALUES","Binding values must list every declared parameter");
        const auto& owned=get(e,"owned");const auto& baseline=get(e,"baseline");if(owned.t!=V::Obj||baseline.t!=V::Obj)fail("BINDINGS","owned and baseline are required");size_t total=0;
        for(const char* c:kOwned){const auto& names=get(owned,c);const auto& digests=get(baseline,c);if(names.t!=V::Arr||digests.t!=V::Obj||names.arr.size()!=digests.obj.size())fail("BINDINGS","Incomplete ownership for "+std::string(c));
            for(const auto& n:names.arr){if(n.t!=V::Str||!namespaced(c,n.str,ns)||!claimed.insert(std::string(c)+":"+n.str).second||!digest(get(digests,n.str.c_str())))fail("BINDINGS","Invalid or overlapping owned "+std::string(c)+" name");++total;}}
        if(total>kMaxEmitted)fail("BINDINGS","Too many owned objects");
    }
}

bool applyParametricProgramCommand(Document& d,const V& c){
    const auto op=get(c,"op").asStr();if(op!="program.bind"&&op!="program.clear"&&op!="program.detach")return false;
    if(d.historyTip||!d.designContainerCheckout.isNull())fail("TARGET","Resume full history and run programs in the owning project, not a scoped container checkout");
    const auto ns=text(c,"namespace");nameSpace(ns);V entries=d.parametricPrograms.isNull()?V::makeArr():d.parametricPrograms;const size_t index=entryIndex(d,ns);
    if(op=="program.bind"){
        only(c,{"op","namespace","program","program_sha256","values"},op);if(index!=entries.arr.size())fail("NAMESPACE","Namespace already has a program; clear or detach it first in the same atomic step");
        const auto& program=get(c,"program");if(get(c,"program_sha256").asStr()!=hash(program))fail("HASH","Reviewed program digest does not match");
        const auto compiled=compileParametricProgram(program,get(c,"values"),ns);const V root=authored(d);const auto& owned=get(compiled,"owned");namespaceFree(d,root,ns,&owned);
        V entry=V::makeObj();entry.set("namespace",V::makeStr(ns));entry.set("name",get(program,"name"));entry.set("program",program);entry.set("program_sha256",get(compiled,"program_sha256"));entry.set("values",get(compiled,"values"));entry.set("owned",owned);entry.set("baseline",fingerprints(root,owned));
        entries.arr.push_back(entry);validateParametricPrograms(entries);d.parametricPrograms=entries;d.programBindingsToSeal.push_back(ns);return true;
    }
    only(c,{"op","namespace","expected_program_sha256"},op);if(index==entries.arr.size())fail("NAMESPACE","No program is installed in namespace "+ns);
    const auto& entry=entries.arr[index];if(get(c,"expected_program_sha256").asStr()!=get(entry,"program_sha256").asStr())fail("STALE_PROGRAM","Inspect the installed program before replacing, removing or detaching it");
    if(op=="program.clear"){const V root=authored(d);if(localEdits(root,entry))fail("LOCAL_EDITS","Program-owned objects were edited locally; detach the program to keep them as ordinary source, or revert the edits");requireNoDependants(d,root,get(entry,"owned"));removeOwned(d,get(entry,"owned"));}
    entries.arr.erase(entries.arr.begin()+(std::ptrdiff_t)index);d.parametricPrograms=entries.arr.empty()?V{}:entries;return true;
}
void sealParametricProgramBindings(Document& d){
    if(d.programBindingsToSeal.empty())return;const auto pending=std::move(d.programBindingsToSeal);d.programBindingsToSeal.clear();
    const auto root=authored(d);for(auto& e:d.parametricPrograms.arr)if(std::find(pending.begin(),pending.end(),get(e,"namespace").asStr())!=pending.end())e.set("baseline",fingerprints(root,get(e,"owned")));
}

bool callParametricProgramTool(AgentSession& s,const std::string& tool,const V& args,V& out){
    if(tool!="parametric_program")return false;only(args,{"action","mode","namespace","program","values","include_program","expected_revision"},"parametric_program");const auto action=text(args,"action");
    if(action=="examples"){out=V::makeObj();out.set("format",V::makeStr("ark.parametric-program.v1"));out.set("examples",parametricProgramExamples());return true;}
    if(action=="validate"){out=compileParametricProgram(get(args,"program"),get(args,"values"),args.find("namespace")?text(args,"namespace",32):"program");out.set("publication",V::makeStr("none: validate compiles source only; prepare evaluates exact geometry for doc_apply"));return true;}
    if(action=="inspect"){
        out=V::makeObj();out.set("revision",V::makeNum((double)s.revision));V rows=V::makeArr();const V root=s.document.parametricPrograms.isNull()?V::makeObj():authored(s.document);
        for(const auto& e:s.document.parametricPrograms.arr){if(args.find("namespace")&&get(e,"namespace").asStr()!=text(args,"namespace",32))continue;
            V row=V::makeObj();for(const char* key:{"namespace","name","program_sha256","values","owned"})row.set(key,get(e,key));row.set("parameters",get(get(e,"program"),"parameters"));row.set("local_edits",V::makeBool(localEdits(root,e)));
            if(get(args,"include_program").b)row.set("program",get(e,"program"));rows.arr.push_back(row);}
        out.set("programs",rows);return true;
    }
    if(action!="prepare")fail("ACTION","Use validate, examples, inspect or prepare");expected(s,args);
    const auto mode=text(args,"mode");if(mode!="install"&&mode!="update"&&mode!="configure"&&mode!="remove"&&mode!="detach")fail("ACTION","Prepare install, update, configure, remove or detach");
    const auto ns=text(args,"namespace",32);nameSpace(ns);const size_t index=entryIndex(s.document,ns);const bool installed=index!=s.document.parametricPrograms.arr.size();
    if(mode=="install"?installed:!installed)fail("NAMESPACE",mode=="install"?"Namespace already has a program; use update, configure, remove or detach":"No program is installed in namespace "+ns);
    if(mode=="install"&&(args.find("include_program")))fail("ACTION","include_program is an inspect option");
    if(args.find("values")&&get(args,"values").t!=V::Obj)fail("VALUES","values must map parameter names to numbers");
    V commands=V::makeArr(),compiled;
    if(installed){const auto& entry=s.document.parametricPrograms.arr[index];
        if(mode!="detach"&&localEdits(authored(s.document),entry))fail("LOCAL_EDITS","Program-owned objects were edited locally; detach the program to keep them as ordinary source, or revert the edits");
        V c=command(mode=="detach"?"program.detach":"program.clear",ns);c.set("expected_program_sha256",get(entry,"program_sha256"));commands.arr.push_back(c);
        if(mode=="update"||mode=="configure"){const V& program=mode=="update"?get(args,"program"):get(entry,"program");if(mode=="configure"&&args.find("program"))fail("ACTION","configure reuses the installed program; use update for new source");
            validateProgram(program);V values=V::makeObj();for(const auto& kv:get(entry,"values").obj)if(get(program,"parameters").find(kv.first.c_str()))values.set(kv.first,kv.second);for(const auto& kv:get(args,"values").obj)values.set(kv.first,kv.second);
            compiled=compileParametricProgram(program,values,ns);}
        else if(args.find("program")||args.find("values"))fail("ACTION",mode+" accepts only namespace and expected_revision");
    }else{compiled=compileParametricProgram(get(args,"program"),get(args,"values"),ns);namespaceFree(s.document,authored(s.document),ns,nullptr);}
    if(compiled.t==V::Obj){for(const auto& cmd:get(compiled,"commands").arr)commands.arr.push_back(cmd);V bind=command("program.bind",ns);const V& program=mode=="configure"?get(s.document.parametricPrograms.arr[index],"program"):get(args,"program");bind.set("program",program);bind.set("program_sha256",get(compiled,"program_sha256"));bind.set("values",get(compiled,"values"));commands.arr.push_back(bind);}
    V apply=V::makeObj();apply.set("commands",commands);apply.set("expected_revision",V::makeNum((double)s.revision));
    // doc_preview executes the real atomic batch on an isolated candidate with
    // exact geometry. Its candidate_id freezes exactly what was reviewed.
    const auto preview=s.call("doc_preview",apply);const auto& evaluated=get(preview,"structuredContent");if(get(preview,"isError").b||!get(evaluated,"valid").b||!evaluated.find("candidate_id"))fail("PREVIEW",evaluated.dump());
    out=evaluated;erase(out,"apply");out.set("namespace",V::makeStr(ns));out.set("mode",V::makeStr(mode));
    if(compiled.t==V::Obj){V report=V::makeObj();for(const char* key:{"name","program_sha256","values","owned","assertions","steps"})report.set(key,get(compiled,key));report.set("emitted",V::makeNum((double)get(compiled,"commands").arr.size()));out.set("program",report);}
    out.set("publication",V::makeStr("doc_apply candidate_id + expected_revision"));return true;
}

void appendParametricProgramTools(V& list){
    V tool=V::makeObj(),schema,annotations=V::makeObj();tool.set("name",V::makeStr("parametric_program"));
    tool.set("description",V::makeStr("Code-defined parametric design (ark.parametric-program.v1): typed parameters, let/set/for/if/assert/call statements and emit templates compile deterministically into native feature.add, sketch.set, plane.set and material.set commands under one namespace. validate compiles without the document; examples lists starter programs; inspect reads installed bindings and local edits; prepare install/update/configure/remove/detach evaluates one isolated exact candidate for explicit doc_apply. Re-runs replace only namespace-owned objects and reject local edits or outside dependants. No file, network or script execution."));
    arkjson::parse(R"({"type":"object","properties":{"action":{"type":"string","enum":["validate","examples","inspect","prepare"]},"mode":{"type":"string","enum":["install","update","configure","remove","detach"]},"namespace":{"type":"string"},"program":{"type":"object"},"values":{"type":"object","additionalProperties":{"type":"number"}},"include_program":{"type":"boolean"},"expected_revision":{"type":"integer","minimum":0}},"required":["action"],"additionalProperties":false})",schema);
    tool.set("inputSchema",schema);annotations.set("readOnlyHint",V::makeBool(false));tool.set("annotations",annotations);list.arr.push_back(tool);
}

V parametricProgramExamples(){
    V out=V::makeArr();
    auto add=[&](const char* id,const char* summary,const char* source){V row=V::makeObj(),program;if(!arkjson::parse(source,program))fail("EXAMPLE",id);validateProgram(program);row.set("id",V::makeStr(id));row.set("summary",V::makeStr(summary));row.set("program",program);out.arr.push_back(row);};
    add("bookshelf","Two sides and evenly pitched shelves as exact native extrusions; loop, function call and a clearance assertion.",R"JSON({"format":"ark.parametric-program.v1","name":"Bookshelf","description":"Board-stock shelf: sides stand on XY behind the XZ front plane, shelves span between them.",
        "parameters":{"width":{"default":800,"min":300,"max":2000,"unit":"mm"},"height":{"default":1800,"min":400,"max":2400,"unit":"mm"},"depth":{"default":300,"min":150,"max":600,"unit":"mm"},"thickness":{"default":18,"min":12,"max":40,"unit":"mm"},"shelves":{"default":5,"min":2,"max":12,"integer":true}},
        "functions":{"shelf":{"params":["k","pitch"],"body":[{"let":"half","value":"width/2-thickness"},{"let":"z0","value":"k*pitch"},
            {"emit":{"op":"sketch.set","name":"shelf{k+1}","sketch":{"plane":"XZ","entities":[{"kind":"wire","name":"outline","start":["{-half}","{z0}"],"segments":[{"kind":"line","to":["{half}","{z0}"]},{"kind":"line","to":["{half}","{z0+thickness}"]},{"kind":"line","to":["{-half}","{z0+thickness}"]},{"kind":"line","to":["{-half}","{z0}"]}]}]}}},
            {"emit":{"op":"feature.add","type":"native_solid","name":"Shelf {k+1}","native":{"op":"extrude"},"p":{"sketch":"shelf{k+1}","depth":"{depth}"}}}]}},
        "body":[{"let":"pitch","value":"(height-thickness)/(shelves-1)"},{"assert":"pitch-thickness>=120","message":"Clear gap between shelves must be at least 120 mm"},
            {"for":"side","count":2,"body":[{"let":"x0","value":"side==0 ? -width/2 : width/2-thickness"},
                {"emit":{"op":"sketch.set","name":"side{side+1}","sketch":{"plane":"XY","entities":[{"kind":"wire","name":"outline","start":["{x0}","{-depth}"],"segments":[{"kind":"line","to":["{x0+thickness}","{-depth}"]},{"kind":"line","to":["{x0+thickness}","0"]},{"kind":"line","to":["{x0}","0"]},{"kind":"line","to":["{x0}","{-depth}"]}]}]}}},
                {"emit":{"op":"feature.add","type":"native_solid","name":"Side {side+1}","native":{"op":"extrude"},"p":{"sketch":"side{side+1}","depth":"{height}"}}}]},
            {"for":"i","count":"shelves","body":[{"call":"shelf","args":{"k":"i","pitch":"pitch"}}]}]})JSON");
    add("bolted_flange","Flange plate with a bore and a bolt circle; one sketch whose holes come from a $for array splice.",R"JSON({"format":"ark.parametric-program.v1","name":"Bolted flange",
        "parameters":{"outer_d":{"default":120,"min":40,"max":600,"unit":"mm"},"bore_d":{"default":40,"min":5,"max":500,"unit":"mm"},"thickness":{"default":12,"min":3,"max":80,"unit":"mm"},"bolt_circle_d":{"default":90,"min":20,"max":580,"unit":"mm"},"hole_d":{"default":9,"min":2,"max":60,"unit":"mm"},"bolts":{"default":6,"min":3,"max":24,"integer":true}},
        "body":[{"assert":"bolt_circle_d-hole_d>=bore_d+4","message":"Bolt holes must keep 2 mm of wall to the bore"},{"assert":"bolt_circle_d+hole_d<=outer_d-4","message":"Bolt holes must keep 2 mm of wall to the rim"},{"assert":"pi*bolt_circle_d/bolts>=hole_d+2","message":"Adjacent bolt holes need 2 mm between them"},
            {"emit":{"op":"sketch.set","name":"outline","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":["0","0","{outer_d/2}"]},{"kind":"circle","name":"bore","dof":["0","0","{bore_d/2}"]},
                {"$for":"k","count":"bolts","item":{"kind":"circle","name":"bolt_{k+1}","dof":["{bolt_circle_d/2*cos(2*pi*k/bolts)}","{bolt_circle_d/2*sin(2*pi*k/bolts)}","{hole_d/2}"]}}]}}},
            {"emit":{"op":"feature.add","type":"native_solid","name":"Flange","native":{"op":"extrude"},"p":{"sketch":"outline","depth":"{thickness}"}}}]})JSON");
    return out;
}
}
