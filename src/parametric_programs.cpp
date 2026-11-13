#include "ark/parametric_programs.hpp"
#include "ark/serialize.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
// One program publishes at most 126 commands so program.clear, the emitted
// source and program.bind fit doc_apply's 128-operation atomic batch.
constexpr size_t kMaxProgramBytes=256*1024,kMaxEmitted=126,kMaxSteps=50000,kMaxLoop=1024,kMaxBlockDepth=32,kMaxCallDepth=16,kMaxStatements=8192,kMaxOutputBytes=2*1024*1024;
constexpr unsigned kMaxExpressionDepth=64;
// Each body nests blocks to 32; the blocks of the body and of all active calls
// together nest to 64 at run time, so stack use does not multiply with the call
// depth and stays well within a 1 MiB thread stack, also in unoptimised builds.
constexpr unsigned kMaxActiveBlocks=64;
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
// Parameter kinds. A number may be bounded and integral; a boolean stores 1/0;
// numeric choices store one listed number; text choices store one listed text,
// read in expressions as its 0-based index and inserted in templates by {@name}.
enum class Kind{Number,Boolean,Choice,Text};
Kind kindOf(const V& spec){if(spec.find("boolean"))return Kind::Boolean;const auto& c=get(spec,"choices");if(c.t==V::Arr&&!c.arr.empty())return c.arr[0].t==V::Str?Kind::Text:Kind::Choice;return Kind::Number;}
const char* kindName(Kind k){return k==Kind::Boolean?"boolean":k==Kind::Choice?"choice":k==Kind::Text?"text_choice":"number";}
int choiceIndex(const V& choices,const V& v){for(size_t k=0;k<choices.arr.size();++k){const auto& x=choices.arr[k];if(x.t==v.t&&(x.t==V::Str?x.str==v.str:x.num==v.num))return (int)k;}return -1;}
bool choiceText(const V& x){if(x.t!=V::Str||x.str.empty()||x.str.size()>64)return false;for(const unsigned char ch:x.str)if(ch<32||ch==127||ch=='{'||ch=='}')return false;return true;}
V parameterKinds(const V& program){V out=V::makeObj();for(const auto& kv:get(program,"parameters").obj)out.set(kv.first,V::makeStr(kindName(kindOf(kv.second))));return out;}
// A typed parameter fixes its own domain, so min, max and integer do not apply.
void typedParameter(const V& spec,const std::string& at){
    for(const char* key:{"min","max","integer"})if(spec.find(key))fail("PARAMETERS",at+": "+key+" does not combine with boolean or choices");
    const auto& d=get(spec,"default");
    if(spec.find("boolean")){if(spec.find("choices")||get(spec,"boolean").t!=V::Bool||!get(spec,"boolean").b)fail("PARAMETERS",at+".boolean must be true and does not combine with choices");if(spec.find("unit"))fail("PARAMETERS",at+": a boolean has no unit");
        if(!(d.t==V::Bool||(d.t==V::Num&&(d.num==0||d.num==1))))fail("PARAMETERS",at+".default must be true, false, 0 or 1");return;}
    const auto& c=get(spec,"choices");if(c.t!=V::Arr||c.arr.empty()||c.arr.size()>64)fail("PARAMETERS",at+".choices must list 1..64 values");
    const bool words=c.arr[0].t==V::Str;std::set<std::string> seen;
    for(const auto& x:c.arr){if(words?!choiceText(x):(x.t!=V::Num||!std::isfinite(x.num)))fail("PARAMETERS",at+".choices must be all finite numbers or all text of 1..64 bytes without braces or control characters");if(!seen.insert(words?x.str:number(x.num)).second)fail("PARAMETERS",at+".choices must be distinct");}
    if(words&&spec.find("unit"))fail("PARAMETERS",at+": text choices have no unit");if(choiceIndex(c,d)<0)fail("PARAMETERS",at+".default must be one of its choices");
}

// Program scopes hold numbers only. Frame 0 holds the immutable parameters.
struct Scope{
    std::vector<std::map<std::string,double>> frames;
    const double* find(const std::string& n) const{for(auto it=frames.rbegin();it!=frames.rend();++it){auto f=it->find(n);if(f!=it->end())return &f->second;}return nullptr;}
};
// Lexical names visible to a statement during validation. Each name keeps a
// stack of binding kinds, innermost last: 'P' parameter, 'S' text-choice
// parameter, 'L' let, 'F' for variable, 'A' function argument, 'T' $for variable.
struct Names{
    std::map<std::string,std::string> kinds;std::vector<std::string> order;
    void push(const std::string& n,char kind){kinds[n]+=kind;order.push_back(n);}
    void popTo(size_t size){while(order.size()>size){auto it=kinds.find(order.back());it->second.pop_back();if(it->second.empty())kinds.erase(it);order.pop_back();}}
    void pop(){popTo(order.size()-1);}
    char bound(const std::string& n) const{const auto it=kinds.find(n);return it==kinds.end()?0:it->second.back();}
};

// Compile-time arithmetic, comparisons, logic and a ternary over program
// numbers. A null scope only checks syntax: identifiers read as 1, or must be
// among the static names when given, and non-finite values are allowed. With a
// scope every intermediate value must be finite; ?:, && and || evaluate only
// the operand they select and syntax-check the others. One nesting budget
// covers parentheses, call arguments, ternaries and unary/power chains, so no
// expression text can exhaust the stack.
class Expr{
    const std::string& s;const Scope* scope;const Names* names;const std::string& at;size_t i=0;unsigned depth=0,skipped=0;
    [[noreturn]] void bad(const std::string& why) const{fail("EXPRESSION",at+": "+why+" in '"+s+"'");}
    bool live() const{return scope&&!skipped;}
    double finite(double v,const char* what) const{if(live()&&!std::isfinite(v))fail("NONFINITE",at+": "+what+" is not finite in '"+s+"'");return v;}
    void nest(){if(++depth>kMaxExpressionDepth)bad("nesting deeper than "+std::to_string(kMaxExpressionDepth));}
    void ws(){while(i<s.size()&&std::isspace((unsigned char)s[i]))++i;}
    bool peek(const char* t){ws();return s.compare(i,std::strlen(t),t)==0;}
    bool eat(const char* t){if(!peek(t))return false;i+=std::strlen(t);return true;}
    double ternary(){nest();const double c=logicalOr();double v=c;if(eat("?")){const double a=branch(c!=0);if(!eat(":"))bad("expected ':'");const double b=branch(c==0);v=c!=0?a:b;}--depth;return v;}
    double branch(bool taken){if(!taken)++skipped;const double v=ternary();if(!taken)--skipped;return v;}
    double logicalOr(){double v=logicalAnd();while(eat("||")){const bool known=v!=0;if(known)++skipped;const double r=logicalAnd();if(known)--skipped;v=(known||r!=0)?1:0;}return v;}
    double logicalAnd(){double v=compare();while(eat("&&")){const bool known=v==0;if(known)++skipped;const double r=compare();if(known)--skipped;v=(!known&&r!=0)?1:0;}return v;}
    double compare(){
        const double a=additive();
        static const char* ops[]={"<=",">=","==","!=","<",">"};
        for(const char* op:ops)if(eat(op)){const double b=additive();for(const char* again:ops)if(peek(again))bad("chain comparisons with &&");
            const std::string o=op;return (o=="<="?a<=b:o==">="?a>=b:o=="=="?a==b:o=="!="?a!=b:o=="<"?a<b:a>b)?1:0;}
        if(peek("=")&&!peek("=="))bad("use == for equality");return a;
    }
    double additive(){double v=multiplicative();for(;;){ws();if(i<s.size()&&s[i]=='+'){++i;v=finite(v+multiplicative(),"a sum");}else if(i<s.size()&&s[i]=='-'){++i;v=finite(v-multiplicative(),"a difference");}else return v;}}
    double multiplicative(){double v=unary();for(;;){ws();if(i>=s.size())return v;const char c=s[i];if(c=='*'){++i;v=finite(v*unary(),"a product");}
        else if(c=='/'||c=='%'){++i;const double d=unary();if(live()&&d==0)fail("NONFINITE",at+": "+(c=='/'?"division":"modulo")+" by zero in '"+s+"'");v=c=='/'?finite(v/d,"a quotient"):finite(v-d*std::floor(v/d),"a remainder");}else return v;}}
    double unary(){ws();if(i<s.size()&&(s[i]=='-'||s[i]=='+'||(s[i]=='!'&&!(i+1<s.size()&&s[i+1]=='=')))){const char op=s[i++];nest();const double v=unary();--depth;return op=='-'?-v:op=='+'?v:v==0?1:0;}return power();}
    double power(){const double b=atom();if(!eat("^"))return b;nest();const double x=unary();--depth;return finite(std::pow(b,x),"a power");}
    double atom(){
        ws();if(i>=s.size())bad("unexpected end");
        if(s[i]=='('){++i;const double v=ternary();if(!eat(")"))bad("expected ')'");return v;}
        if(std::isdigit((unsigned char)s[i])||s[i]=='.'){
            const size_t start=i;bool digits=false;while(i<s.size()&&std::isdigit((unsigned char)s[i])){++i;digits=true;}
            if(i<s.size()&&s[i]=='.'){++i;while(i<s.size()&&std::isdigit((unsigned char)s[i])){++i;digits=true;}}
            if(!digits)bad("invalid number");
            if(i<s.size()&&(s[i]=='e'||s[i]=='E')){size_t j=i+1;if(j<s.size()&&(s[j]=='+'||s[j]=='-'))++j;if(j<s.size()&&std::isdigit((unsigned char)s[j])){i=j;while(i<s.size()&&std::isdigit((unsigned char)s[i]))++i;}}
            return finite(std::strtod(s.substr(start,i-start).c_str(),nullptr),"a number literal");
        }
        if(!(std::isalpha((unsigned char)s[i])||s[i]=='_'))bad("unexpected '"+s.substr(i,1)+"'");
        const size_t start=i;while(i<s.size()&&(std::isalnum((unsigned char)s[i])||s[i]=='_'))++i;const auto id=s.substr(start,i-start);
        if(eat("(")){std::vector<double> args;if(!eat(")")){do args.push_back(ternary());while(eat(","));if(!eat(")"))bad("expected ')' after arguments of "+id);}const double r=call(id,args);if(live()&&!std::isfinite(r))fail("NONFINITE",at+": "+id+"(...) is not finite in '"+s+"'");return r;}
        if(id=="pi")return 3.14159265358979323846;if(id=="e")return 2.71828182845904523536;if(id=="true")return 1;if(id=="false")return 0;
        if(names&&!names->bound(id))bad("unknown identifier '"+id+"'");
        if(!live())return 1;if(const double* v=scope->find(id))return *v;bad("unknown identifier '"+id+"'");
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
    Expr(const std::string& source,const Scope* values,const std::string& where,const Names* visible=nullptr):s(source),scope(values),names(visible),at(where){}
    double run(){if(s.empty()||s.size()>2048)bad("expressions need 1..2048 bytes");const double v=ternary();ws();if(i!=s.size())bad("unexpected '"+s.substr(i,1)+"'");if(scope&&!std::isfinite(v))fail("NONFINITE",at+": '"+s+"' is not a finite number");return v;}
};

// "{expr}" alone becomes a JSON number; mixed text interpolates the shortest
// round-trip value. "{{" and "}}" are literal braces. Other strings pass
// unchanged, so document parameter expressions keep their native meaning.
// Driven sketch coordinates always stay text: a numeric sketch coordinate is a
// free solver variable, while text is driven exactly. "{@name}" inserts the
// chosen text of a text-choice parameter and always yields text.
V templated(const std::string& source,const Scope* scope,const std::string& at,bool driven=false,const Names* names=nullptr,const std::map<std::string,std::string>* texts=nullptr){
    if(source.find_first_of("{}")==std::string::npos)return V::makeStr(source);
    std::string out;size_t placeholders=0;bool literal=false;double single=0;
    for(size_t i=0;i<source.size();){
        if(source.compare(i,2,"{{")==0){out+='{';i+=2;literal=true;continue;}
        if(source.compare(i,2,"}}")==0){out+='}';i+=2;literal=true;continue;}
        if(source[i]=='}')fail("TEMPLATE",at+": unmatched '}' in '"+source+"'");
        if(source[i]!='{'){out+=source[i++];literal=true;continue;}
        const auto end=source.find('}',i+1);if(end==std::string::npos)fail("TEMPLATE",at+": unclosed '{' in '"+source+"'");
        if(source[i+1]=='@'){const auto id=source.substr(i+2,end-i-2);if(!identifier(id))fail("TEMPLATE",at+": {@name} needs a parameter name in '"+source+"'");
            if(names&&names->bound(id)!='S')fail("TEMPLATE",at+": {@"+id+"} needs a text-choice parameter; "+id+(names->bound(id)?" is a number here":" is unknown")+" in '"+source+"'");
            if(scope){if(!texts||!texts->count(id))fail("TEMPLATE",at+": {@"+id+"} needs a text-choice parameter in '"+source+"'");out+=texts->at(id);}else out+=id;
            literal=true;i=end+1;continue;}
        single=Expr(source.substr(i+1,end-i-1),scope,at,names).run();++placeholders;out+=number(single);i=end+1;
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
void checkValue(const V& v,const std::string& at,const Names* names){if(v.t==V::Num){if(!std::isfinite(v.num))fail("NONFINITE",at);return;}if(v.t!=V::Str)fail("SYNTAX",at+" needs a number or expression text");Expr(v.str,nullptr,at,names).run();}
void checkTemplate(const V& v,const std::string& at,unsigned depth,Names* names){
    if(depth>64)fail("SYNTAX",at+": emitted value nesting deeper than 64");
    if(v.t==V::Str)templated(v.str,nullptr,at,false,names);
    else if(v.t==V::Arr){for(size_t k=0;k<v.arr.size();++k){const auto& item=v.arr[k];const auto where=at+"["+std::to_string(k)+"]";
        if(item.t==V::Obj&&item.find("$for")){only(item,{"$for","count","item"},where);if(get(item,"$for").t!=V::Str||!identifier(get(item,"$for").str))fail("SYNTAX",where+".$for needs an identifier");checkValue(get(item,"count"),where+".count",names);if(!item.find("item"))fail("SYNTAX",where+" needs item");
            if(names)names->push(get(item,"$for").str,'T');checkTemplate(get(item,"item"),where+".item",depth+1,names);if(names)names->pop();}
        else checkTemplate(item,where,depth+1,names);}}
    else if(v.t==V::Obj)for(const auto& kv:v.obj){if(!kv.first.empty()&&kv.first[0]=='$')fail("SYNTAX",at+": unknown directive "+kv.first);checkTemplate(kv.second,at+"."+kv.first,depth+1,names);}
}
// With names, every identifier resolves lexically before anything runs, so a
// typo in a branch the current values skip still rejects. A let is visible
// after its own value; blocks, loop bodies and $for items end their names.
void checkBlock(const V& body,const V& functions,const std::set<std::string>& parameters,const std::string& at,unsigned depth,size_t& total,Names* names){
    if(body.t!=V::Arr)fail("SYNTAX",at+" must be an array of statements");if(depth>kMaxBlockDepth)fail("SYNTAX",at+": blocks nest deeper than "+std::to_string(kMaxBlockDepth));
    const size_t visible=names?names->order.size():0;
    for(size_t k=0;k<body.arr.size();++k){
        const auto& st=body.arr[k];const auto where=at+"["+std::to_string(k)+"]";const std::string kind=statementKind(st,where);
        if(++total>kMaxStatements)fail("CAPACITY","Programs hold at most "+std::to_string(kMaxStatements)+" statements");
        if(kind=="let"||kind=="set"){only(st,{kind.c_str(),"value"},where);const auto& name=get(st,kind.c_str());if(name.t!=V::Str||!identifier(name.str))fail("SYNTAX",where+"."+kind+" needs an identifier");if(parameters.count(name.str))fail("SYNTAX",where+": parameter "+name.str+" is immutable");
            if(kind=="set"&&names){const char target=names->bound(name.str);if(target!='L')fail("SYNTAX",where+": set "+name.str+(target=='F'?" names a for variable, which is read-only":target=='A'?" names a function argument, which is read-only; copy it with let first":" has no earlier let in scope"));}
            checkValue(get(st,"value"),where+".value",names);if(kind=="let"&&names)names->push(name.str,'L');}
        else if(kind=="for"){only(st,{"for","count","body"},where);const auto& name=get(st,"for");if(name.t!=V::Str||!identifier(name.str)||parameters.count(name.str))fail("SYNTAX",where+".for needs an identifier that is not a parameter");checkValue(get(st,"count"),where+".count",names);
            if(names)names->push(name.str,'F');checkBlock(get(st,"body"),functions,parameters,where+".body",depth+1,total,names);if(names)names->pop();}
        else if(kind=="if"){only(st,{"if","then","else"},where);checkValue(get(st,"if"),where+".if",names);checkBlock(get(st,"then"),functions,parameters,where+".then",depth+1,total,names);if(st.find("else"))checkBlock(get(st,"else"),functions,parameters,where+".else",depth+1,total,names);}
        else if(kind=="assert"){only(st,{"assert","message"},where);checkValue(get(st,"assert"),where+".assert",names);if(st.find("message"))text(st,"message",256);}
        else if(kind=="emit"){only(st,{"emit"},where);const auto& c=get(st,"emit");if(c.t!=V::Obj||get(c,"op").t!=V::Str)fail("SYNTAX",where+".emit needs a command object with op");checkTemplate(c,where+".emit",0,names);}
        else{only(st,{"call","args"},where);const auto& name=get(st,"call");const auto* fn=name.t==V::Str?functions.find(name.str.c_str()):nullptr;if(!fn)fail("SYNTAX",where+": unknown function "+name.asStr());
            const auto& args=get(st,"args");if(!args.isNull()&&args.t!=V::Obj)fail("SYNTAX",where+".args must be an object");std::set<std::string> expected,given;for(const auto& p:get(*fn,"params").arr)expected.insert(p.str);for(const auto& kv:args.obj){given.insert(kv.first);checkValue(kv.second,where+".args."+kv.first,names);}
            if(expected!=given)fail("SYNTAX",where+": call "+name.str+" must pass exactly its declared params");}
    }
    if(names)names->popTo(visible);
}
// Compilation resolves every name statically. Stored bindings are checked on
// load and save without name resolution, so a document saved before that
// check still opens and saves; running such a program rejects explicitly.
void validateProgram(const V& p,bool resolveNames=true){
    if(p.t!=V::Obj||get(p,"format").asStr()!="ark.parametric-program.v1")fail("FORMAT","Expected an object with format ark.parametric-program.v1");
    if(p.dump().size()>kMaxProgramBytes)fail("CAPACITY","Program source exceeds 256 KiB");
    only(p,{"format","name","description","parameters","functions","body"},"program");text(p,"name");if(p.find("description"))text(p,"description",2048);
    const auto& parameters=get(p,"parameters");if(!parameters.isNull()&&(parameters.t!=V::Obj||parameters.obj.size()>64))fail("PARAMETERS","parameters must be an object of at most 64 entries");
    std::set<std::string> names,texts;
    for(const auto& kv:parameters.obj){const auto at="parameters."+kv.first;if(!identifier(kv.first))fail("PARAMETERS",at+": names are identifiers");const auto& spec=kv.second;if(spec.t!=V::Obj)fail("PARAMETERS",at+" must be an object");only(spec,{"default","min","max","integer","unit","description","boolean","choices"},at);
        if(spec.find("boolean")||spec.find("choices")){if(!spec.find("default"))fail("PARAMETERS",at+" needs a default");if(spec.find("unit"))text(spec,"unit",16);if(spec.find("description"))text(spec,"description",512);
            typedParameter(spec,at);if(kindOf(spec)==Kind::Text)texts.insert(kv.first);names.insert(kv.first);continue;}
        for(const char* key:{"default","min","max"})if(spec.find(key)&&(get(spec,key).t!=V::Num||!std::isfinite(get(spec,key).num)))fail("PARAMETERS",at+"."+key+" must be a finite number");
        if(!spec.find("default"))fail("PARAMETERS",at+" needs a default");if(spec.find("integer")&&get(spec,"integer").t!=V::Bool)fail("PARAMETERS",at+".integer must be boolean");
        if(spec.find("unit"))text(spec,"unit",16);if(spec.find("description"))text(spec,"description",512);
        if(spec.find("min")&&spec.find("max")&&get(spec,"min").num>get(spec,"max").num)fail("PARAMETERS",at+": min exceeds max");names.insert(kv.first);}
    const auto& functions=get(p,"functions");if(!functions.isNull()&&(functions.t!=V::Obj||functions.obj.size()>64))fail("FUNCTIONS","functions must be an object of at most 64 entries");
    size_t total=0;Names visible;for(const auto& n:names)visible.push(n,texts.count(n)?'S':'P');
    for(const auto& kv:functions.obj){const auto at="functions."+kv.first;if(!identifier(kv.first))fail("FUNCTIONS",at+": names are identifiers");const auto& fn=kv.second;if(fn.t!=V::Obj)fail("FUNCTIONS",at+" must be an object");only(fn,{"params","body","description"},at);
        const auto& params=get(fn,"params");if(!params.isNull()&&(params.t!=V::Arr||params.arr.size()>16))fail("FUNCTIONS",at+".params must list at most 16 identifiers");std::set<std::string> seen;
        for(const auto& name:params.arr)if(name.t!=V::Str||!identifier(name.str)||names.count(name.str)||!seen.insert(name.str).second)fail("FUNCTIONS",at+".params must be distinct identifiers that are not parameters");
        // A function sees the parameters, its arguments and its own lets, as at run time.
        Names local=visible;for(const auto& name:params.arr)local.push(name.str,'A');
        if(fn.find("description"))text(fn,"description",512);checkBlock(get(fn,"body"),functions,names,at+".body",1,total,resolveNames?&local:nullptr);}
    checkBlock(get(p,"body"),functions,names,"body",1,total,resolveNames?&visible:nullptr);if(get(p,"body").arr.empty())fail("SYNTAX","body needs at least one statement");
}
// Complete, validated public values: declared parameters only, finite,
// within their bounds and integral where requested. Booleans store 1/0 and
// choices store the listed value, so stored values resolve to themselves.
V resolveValues(const V& program,const V& values){
    if(!values.isNull()&&values.t!=V::Obj)fail("VALUES","values must map parameter names to numbers, booleans or choice text");
    for(const auto& kv:values.obj)if(!get(program,"parameters").find(kv.first.c_str()))fail("VALUES","Undeclared parameter "+kv.first);
    V out=V::makeObj();
    for(const auto& kv:get(program,"parameters").obj){const auto& spec=kv.second;const V* given=values.find(kv.first.c_str());const V& v=given?*given:get(spec,"default");const Kind kind=kindOf(spec);
        if(kind==Kind::Boolean){if(!(v.t==V::Bool||(v.t==V::Num&&(v.num==0||v.num==1))))fail("VALUES",kv.first+" is boolean: use true, false, 0 or 1");out.set(kv.first,V::makeNum((v.t==V::Bool?v.b:v.num!=0)?1:0));continue;}
        if(kind!=Kind::Number){const auto& choices=get(spec,"choices");const int k=choiceIndex(choices,v);if(k<0)fail("VALUES",kv.first+"="+(v.t==V::Num?number(v.num):v.dump())+" is not one of its choices "+choices.dump());out.set(kv.first,choices.arr[(size_t)k]);continue;}
        if(v.t!=V::Num||!std::isfinite(v.num))fail("VALUES",kv.first+" needs a finite number");
        if(spec.find("min")&&v.num<get(spec,"min").num)fail("VALUES",kv.first+"="+number(v.num)+" is below min "+number(get(spec,"min").num));
        if(spec.find("max")&&v.num>get(spec,"max").num)fail("VALUES",kv.first+"="+number(v.num)+" exceeds max "+number(get(spec,"max").num));
        if(get(spec,"integer").b&&v.num!=std::floor(v.num))fail("VALUES",kv.first+" must be an integer");out.set(kv.first,v);}
    canonical(out);return out;
}

struct Compiler{
    const V& program;Scope scope;std::set<std::string> parameters;std::map<std::string,std::string> texts;size_t steps=0,bytes=0;unsigned calls=0,active=0;
    std::vector<V> emitted;V assertions=V::makeArr();
    explicit Compiler(const V& p):program(p){}
    void step(const std::string& at){if(++steps>kMaxSteps)fail("STEPS",at+": program exceeds "+std::to_string(kMaxSteps)+" evaluation steps");}
    double value(const V& v,const std::string& at){if(v.t==V::Num)return v.num;return Expr(v.str,&scope,at).run();}
    size_t count(const V& v,const std::string& at){const double n=value(v,at);if(n!=std::floor(n)||n<0||n>(double)kMaxLoop)fail("LOOP",at+" must be an integer count 0.."+std::to_string(kMaxLoop)+", got "+number(n));return (size_t)n;}
    V expand(const V& v,const std::string& at,Emit mode){
        if(v.t==V::Str)return templated(v.str,&scope,at,mode==Emit::Driven,nullptr,&texts);
        if(v.t==V::Arr){V out=V::makeArr();for(size_t k=0;k<v.arr.size();++k){const auto& item=v.arr[k];const auto where=at+"["+std::to_string(k)+"]";
            if(item.t==V::Obj&&item.find("$for")){const auto n=count(get(item,"count"),where+".count");for(size_t j=0;j<n;++j){step(where);scope.frames.push_back({{get(item,"$for").str,(double)j}});out.arr.push_back(expand(get(item,"item"),where+".item",mode));scope.frames.pop_back();}}
            else out.arr.push_back(expand(item,where,mode));}return out;}
        if(v.t==V::Obj){V out=V::makeObj();for(const auto& kv:v.obj)out.set(kv.first,expand(kv.second,at+"."+kv.first,childMode(mode,kv.first)));return out;}
        return v;
    }
    V command(const V& source,const std::string& at){V out=V::makeObj();const bool sketch=get(source,"op").asStr()=="sketch.set";for(const auto& kv:source.obj)out.set(kv.first,expand(kv.second,at+"."+kv.first,sketch&&kv.first=="sketch"?Emit::Sketch:Emit::Plain));return out;}
    void block(const V& body,const std::string& at){if(++active>kMaxActiveBlocks)fail("RECURSION",at+": blocks nest deeper than "+std::to_string(kMaxActiveBlocks)+" across calls");
        scope.frames.emplace_back();for(size_t k=0;k<body.arr.size();++k)statement(body.arr[k],at+"["+std::to_string(k)+"]");scope.frames.pop_back();--active;}
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
        // A key that accepts a sketch or a feature must not pick one silently.
        const char* match=nullptr;for(const char* c:found->second){const auto l=locals.find(c);if(l!=locals.end()&&l->second.count(v.str)){if(match)fail("REFERENCE",at+": "+key+" '"+v.str+"' names both a program-local sketch and a program-local feature; give them distinct local names");match=c;}}
        if(match)v.str=prefixed(match,v.str,ns);}
    else if(v.t==V::Num&&featureKey(key))fail("REFERENCE",at+": "+key+" must name a feature; numeric IDs are not stable program identities");
    else if(v.t==V::Arr)for(auto& item:v.arr)rewrite(item,key,datumKeys,locals,ns,at);
    else if(v.t==V::Obj)for(auto& kv:v.obj)rewrite(kv.second,kv.first,datumKeys,locals,ns,at+"."+kv.first);
}
const char* collectionFor(const std::string& op){return op=="feature.add"?"features":op=="sketch.set"?"sketches":op=="plane.set"?"planes":op=="material.set"?"materials":nullptr;}

// The checked authoring value equals parsing documentToJson(d,false,false),
// without materializing and reparsing the text.
V authored(const Document& d){try{return documentValue(d,false,false);}catch(const std::exception& e){fail("SOURCE",std::string("Cannot serialize authored source: ")+e.what());}}
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
    // Skeleton source is text; a program-owned name anywhere in it is treated as
    // a dependency. Besides the serialized form, search every raw string and its
    // JSON-unescaped form, because Skeleton names quoted names as JSON literals
    // ("Post \"A\"", or with backslash-u escapes) that neither raw nor serialized text contains.
    if(const auto* skeleton=root.find("skeleton")){std::vector<std::string> texts{skeleton->dump()};
        auto unescaped=[](const std::string& s){std::string out;for(size_t i=0;i<s.size();){bool decoded=false;if(s[i]=='\\'&&i+1<s.size())for(size_t n:{size_t(12),size_t(6),size_t(2)}){if((n==2)==(s[i+1]=='u')||i+n>s.size())continue;V literal;if(arkjson::parse("\""+s.substr(i,n)+"\"",literal)&&literal.t==V::Str){out+=literal.str;i+=n;decoded=true;break;}}if(!decoded)out+=s[i++];}return out;};
        std::function<void(const V&)> collect=[&](const V& v){if(v.t==V::Str){texts.push_back(v.str);texts.push_back(unescaped(v.str));}else if(v.t==V::Arr)for(const auto& item:v.arr)collect(item);else if(v.t==V::Obj)for(const auto& kv:v.obj){texts.push_back(kv.first);collect(kv.second);}};
        collect(*skeleton);for(const auto& n:names)for(const auto& t:texts)if(t.find(n)!=std::string::npos)fail("DEPENDANTS","Skeleton source references program-owned "+n+"; edit the Skeleton first, or detach the program");}
    // Saved configurations contain labels and numeric inputs, never references
    // to generated feature identities. A label matching a feature is not a user
    // of that feature and must not block an atomic program re-run.
    for(const auto& kv:root.obj){if(kv.first=="parametric_programs"||kv.first=="design_variants"||kv.first=="skeleton")continue;
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
std::string canonicalText(V v){canonical(v);return v.dump();}
// Commands whose name or id designates a feature, sketch, datum, material,
// parameter or canvas, and program operations, can create, edit, rename or
// remove namespace objects. Assembly nodes, mates, drawings and saved variants
// have their own names; edits of owned rows by numeric id are caught when the
// batch is sealed.
bool editsNamespace(const V& c,const std::string& ns){
    const auto op=get(c,"op").asStr();if(op.rfind("program.",0)==0)return get(c,"namespace").asStr()==ns;
    static const char* families[]={"feature.","sketch.","plane.","material.","param.","sheet.","frame.","canvas."};
    if(std::none_of(std::begin(families),std::end(families),[&](const char* f){return op.rfind(f,0)==0;}))return false;
    for(const char* key:{"name","id"}){const auto& v=get(c,key);if(v.t==V::Str&&(namespaced("features",v.str,ns)||namespaced("sketches",v.str,ns)))return true;}
    return false;
}
bool existing(const Document& d,const std::string& collection,const std::string& name){
    if(collection=="features")return std::any_of(d.features.begin(),d.features.end(),[&](const Feature& f){return f.name==name;});
    if(collection=="sketches")return std::any_of(d.sketches.begin(),d.sketches.end(),[&](const NamedSketch& s){return s.name==name;});
    if(collection=="planes")return std::any_of(d.planes.begin(),d.planes.end(),[&](const DatumPlane& p){return p.name==name;});
    return std::any_of(d.materials.begin(),d.materials.end(),[&](const Material& m){return m.name==name;});
}

// .arkprog exchange. Only the tool reads and writes these files, and only with
// a .arkprog extension; the program language itself never touches files.
constexpr size_t kMaxProgramFileBytes=256*1024;
const char* kProgramFile="ark.parametric-program-file.v1";
bool publishProgramFile(const std::filesystem::path& staged,const std::filesystem::path& target,bool replace);
std::string utf8(const std::filesystem::path& p){const auto s=p.u8string();return {s.begin(),s.end()};}
std::filesystem::path programFile(const V& args,const char* key){
    const auto& p=get(args,key);if(p.t!=V::Str||p.str.empty()||p.str.size()>4096||p.str.find('\0')!=std::string::npos)fail("FILE",std::string(key)+" must name a .arkprog file");
    // A relative path would depend on the host's working directory.
    if(!std::filesystem::u8path(p.str).is_absolute())fail("FILE",std::string(key)+" must be an absolute path");const auto path=std::filesystem::u8path(p.str).lexically_normal();auto extension=utf8(path.extension());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)std::tolower(c);});
    if(extension!=".arkprog")fail("FILE",std::string(key)+" must end in .arkprog");return path;
}
std::string readProgramBytes(const std::filesystem::path& path){
    std::error_code ec;if(!std::filesystem::is_regular_file(path,ec))fail("FILE","No .arkprog file at "+utf8(path));std::ifstream in(path,std::ios::binary);if(!in)fail("FILE","Cannot read "+utf8(path));
    std::string bytes(kMaxProgramFileBytes+1,'\0');in.read(&bytes[0],(std::streamsize)bytes.size());bytes.resize((size_t)in.gcount());if(bytes.size()>kMaxProgramFileBytes)fail("CAPACITY",".arkprog files hold at most 256 KiB");return bytes;
}
// The file supplies source only; the caller still validates, compiles and reviews it.
V programFromFile(const V& args){
    const auto bytes=readProgramBytes(programFile(args,"program_path"));V file;if(!arkjson::parse(bytes,file)||file.t!=V::Obj)fail("FILE","program_path does not hold a JSON object");
    if(get(file,"format").asStr()!=kProgramFile)fail("FORMAT","Expected a .arkprog file with format ark.parametric-program-file.v1");only(file,{"format","program","program_sha256"},"program file");
    if(!digest(get(file,"program_sha256"))||get(file,"program_sha256").str!=hash(get(file,"program")))fail("HASH","The .arkprog program_sha256 does not match its program");return get(file,"program");
}
// Writes canonical JSON (sorted keys, no whitespace) through a staged file and
// one rename, refusing to replace an existing file unless overwrite is true.
void exportProgram(const AgentSession& s,const V& args,V& out){
    only(args,{"action","namespace","path","overwrite","expected_revision"},"parametric_program export");expected(s,args);
    const auto ns=text(args,"namespace",32);nameSpace(ns);const size_t index=entryIndex(s.document,ns);if(index==s.document.parametricPrograms.arr.size())fail("NAMESPACE","No program is installed in namespace "+ns);
    const auto& overwrite=get(args,"overwrite");if(!overwrite.isNull()&&overwrite.t!=V::Bool)fail("ACTION","overwrite must be boolean");const auto path=programFile(args,"path");const auto& entry=s.document.parametricPrograms.arr[index];
    if(get(entry,"program_sha256").asStr()!=hash(get(entry,"program")))fail("HASH","Installed program digest does not match");
    V file=V::makeObj();file.set("format",V::makeStr(kProgramFile));file.set("program",get(entry,"program"));file.set("program_sha256",get(entry,"program_sha256"));canonical(file);const auto bytes=file.dump();
    if(bytes.size()>kMaxProgramFileBytes)fail("CAPACITY","The .arkprog file would exceed 256 KiB");
    std::error_code ec;const bool exists=std::filesystem::exists(path,ec);if(exists&&!overwrite.b)fail("FILE_EXISTS",utf8(path)+" already exists; choose a new path or pass overwrite:true");
    if(exists&&!std::filesystem::is_regular_file(path,ec))fail("FILE","Only a regular .arkprog file can be replaced");if(!std::filesystem::is_directory(path.parent_path(),ec))fail("FILE","The destination directory does not exist");
    std::random_device entropy;const auto staged=path.parent_path()/(".arkprog-"+std::to_string(entropy())+"-"+std::to_string(entropy())+".tmp");
    {std::ofstream o(staged,std::ios::binary|std::ios::trunc);o.write(bytes.data(),(std::streamsize)bytes.size());o.flush();const bool written=(bool)o;o.close();if(!written||o.fail()){std::filesystem::remove(staged,ec);fail("FILE","Cannot write the staged .arkprog file");}}
    if(!publishProgramFile(staged,path,overwrite.b)){std::filesystem::remove(staged,ec);fail(std::filesystem::exists(path,ec)&&!overwrite.b?"FILE_EXISTS":"FILE","Could not publish "+utf8(path)+"; nothing was replaced");}
    if(readProgramBytes(path)!=bytes)fail("FILE","The published .arkprog file does not read back identically");
    out=V::makeObj();out.set("format",V::makeStr(kProgramFile));out.set("path",V::makeStr(utf8(path)));out.set("namespace",V::makeStr(ns));out.set("name",get(entry,"name"));out.set("program_sha256",get(entry,"program_sha256"));
    out.set("file_sha256",V::makeStr(iterationSha256(bytes)));out.set("bytes",V::makeNum((double)bytes.size()));out.set("replaced",V::makeBool(exists));out.set("revision",V::makeNum((double)s.revision));out.set("publication",V::makeStr("file only; the document and its revision are unchanged"));
}
}

std::string parametricProgramHash(const V& program){return hash(program);}

V compileParametricProgram(const V& program,const V& values,const std::string& ns){
    nameSpace(ns);validateProgram(program);const V resolved=resolveValues(program,values);
    Compiler c(program);std::map<std::string,double> params;
    for(const auto& kv:resolved.obj){if(kv.second.t==V::Str){params[kv.first]=(double)choiceIndex(get(get(get(program,"parameters"),kv.first.c_str()),"choices"),kv.second);c.texts[kv.first]=kv.second.str;}else params[kv.first]=kv.second.num;c.parameters.insert(kv.first);}c.scope.frames.push_back(std::move(params));
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
    r.set("values",resolved);r.set("parameters",get(program,"parameters").isNull()?V::makeObj():get(program,"parameters"));r.set("parameter_kinds",parameterKinds(program));r.set("commands",commands);r.set("owned",owned);r.set("assertions",c.assertions);r.set("steps",V::makeNum((double)c.steps));
    r.set("geometry_authority",V::makeStr("none until doc_preview/doc_apply evaluates the emitted native commands"));return r;
}

void validateParametricPrograms(const V& entries){
    if(entries.isNull())return;if(entries.t!=V::Arr||entries.arr.empty()||entries.arr.size()>64)fail("BINDINGS","Expected 1..64 program bindings");std::set<std::string> namespaces,claimed;
    for(const auto& e:entries.arr){
        if(e.t!=V::Obj)fail("BINDINGS","Binding must be an object");only(e,{"namespace","name","program","program_sha256","values","owned","baseline"},"binding");
        const auto ns=text(e,"namespace");nameSpace(ns);if(!namespaces.insert(ns).second)fail("BINDINGS","Duplicate namespace "+ns);
        const auto& program=get(e,"program");validateProgram(program,false);if(get(e,"program_sha256").asStr()!=hash(program))fail("HASH","Embedded program digest does not match");
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
void verifyParametricProgramBatch(const Document& d,const V& commands,size_t& index){
    if(commands.t!=V::Arr)return;std::set<std::string> bound;
    for(size_t b=0;b<commands.arr.size();++b){
        const auto& bind=commands.arr[b];if(get(bind,"op").asStr()!="program.bind")continue;index=b;
        only(bind,{"op","namespace","program","program_sha256","values"},"program.bind");const auto ns=text(bind,"namespace");nameSpace(ns);
        if(!bound.insert(ns).second)fail("PROVENANCE","Namespace "+ns+" is bound twice in one batch");
        const auto& program=get(bind,"program");if(get(bind,"program_sha256").asStr()!=hash(program))fail("HASH","Reviewed program digest does not match");
        // The bind publishes exactly what its reviewed source compiles to: the
        // compiled commands, unchanged and in order, immediately before it.
        const auto compiled=compileParametricProgram(program,get(bind,"values"),ns);const auto& emitted=get(compiled,"commands").arr;
        if(emitted.size()>b)fail("PROVENANCE","program.bind for "+ns+" must follow its "+std::to_string(emitted.size())+" compiled commands; publish the prepared candidate or the exact validate output");
        size_t start=b-emitted.size();
        for(size_t k=0;k<emitted.size();++k)if(canonicalText(commands.arr[start+k])!=canonicalText(emitted[k])){index=start+k;fail("PROVENANCE","Command "+std::to_string(start+k)+" differs from compiled command "+std::to_string(k+1)+" of the "+ns+" program; publish the prepared candidate or the exact validate output");}
        const bool cleared=start>0&&get(commands.arr[start-1],"op").asStr()=="program.clear"&&get(commands.arr[start-1],"namespace").asStr()==ns;if(cleared)--start;
        for(size_t i=0;i<commands.arr.size();++i)if((i<start||i>b)&&editsNamespace(commands.arr[i],ns)){index=i;fail("PROVENANCE","Command "+std::to_string(i)+" ("+get(commands.arr[i],"op").asStr()+") changes namespace "+ns+" outside its compiled program run; program-owned objects come only from the bound source");}
        // Upserts merge into an existing row; compiled output must start empty.
        const auto* installed=entryIndex(d,ns)<d.parametricPrograms.arr.size()?&d.parametricPrograms.arr[entryIndex(d,ns)]:nullptr;
        for(const char* c:kOwned)for(const auto& n:get(get(compiled,"owned"),c).arr){bool replaced=false;if(cleared&&installed)for(const auto& old:get(get(*installed,"owned"),c).arr)replaced|=old.str==n.str;
            if(!replaced&&existing(d,c,n.str))fail("PROVENANCE","Existing "+std::string(c)+" "+n.str+" is not owned by the "+ns+" program and would merge into its compiled output; rename or remove it first");}
    }
}
void verifyParametricProgramSeal(const Document& d){
    if(d.programBindingsToSeal.empty())return;const auto root=authored(d);
    for(const auto& e:d.parametricPrograms.arr){const auto ns=get(e,"namespace").asStr();if(std::find(d.programBindingsToSeal.begin(),d.programBindingsToSeal.end(),ns)==d.programBindingsToSeal.end())continue;
        // The baseline holds the fingerprints taken by program.bind, or resealed
        // by an earlier evaluation of this same batch.
        bool same=false;try{same=hash(fingerprints(root,get(e,"owned")))==hash(get(e,"baseline"));}catch(const std::exception&){}
        if(!same)fail("PROVENANCE","A later command in this batch edited, renamed or removed objects owned by the "+ns+" program; publish its compiled output unchanged and edit it in a separate step");}
}
// Intermediate evaluations (assembly.drive, component.*, motion, drag chains)
// reseal too, so the batch keeps its bound namespaces until doc_apply publishes
// and every later evaluation verifies them again.
void sealParametricProgramBindings(Document& d){
    if(d.programBindingsToSeal.empty())return;const auto& pending=d.programBindingsToSeal;
    const auto root=authored(d);for(auto& e:d.parametricPrograms.arr)if(std::find(pending.begin(),pending.end(),get(e,"namespace").asStr())!=pending.end())e.set("baseline",fingerprints(root,get(e,"owned")));
}
// Earlier readers reject boolean/choices fields and text variant values, so
// only documents that store them select version 60; others keep 55 or 56.
bool parametricProgramsRequire60(const Document& d){
    for(const auto& e:d.parametricPrograms.arr)for(const auto& kv:get(get(e,"program"),"parameters").obj)if(kv.second.find("boolean")||kv.second.find("choices"))return true;
    for(const auto& v:d.designVariants.arr)for(const auto& kv:get(v,"values").obj)if(kv.second.t!=V::Num)return true;
    return false;
}

bool callParametricProgramTool(AgentSession& s,const std::string& tool,const V& request,V& out){
    if(tool!="parametric_program")return false;only(request,{"action","mode","namespace","program","program_path","values","include_program","path","overwrite","expected_revision"},"parametric_program");const auto action=text(request,"action");
    if(action=="export"){exportProgram(s,request,out);return true;}
    if((request.find("path")||request.find("overwrite")))fail("ACTION","path and overwrite are export options");
    // program_path reads a .arkprog file in place of an inline program.
    V args=request;if(request.find("program_path")){if(action!="validate"&&action!="prepare")fail("ACTION","program_path is a validate or prepare option");if(request.find("program"))fail("ACTION","Use program or program_path, not both");args.set("program",programFromFile(request));erase(args,"program_path");}
    if(action=="examples"){out=V::makeObj();out.set("format",V::makeStr("ark.parametric-program.v1"));out.set("examples",parametricProgramExamples());return true;}
    if(action=="validate"){out=compileParametricProgram(get(args,"program"),get(args,"values"),args.find("namespace")?text(args,"namespace",32):"program");out.set("publication",V::makeStr("none: validate compiles source only; prepare evaluates exact geometry for doc_apply"));if(request.find("program_path"))out.set("program_path",V::makeStr(utf8(programFile(request,"program_path"))));return true;}
    if(action=="inspect"){
        out=V::makeObj();out.set("revision",V::makeNum((double)s.revision));V rows=V::makeArr();const V root=s.document.parametricPrograms.isNull()?V::makeObj():authored(s.document);
        for(const auto& e:s.document.parametricPrograms.arr){if(args.find("namespace")&&get(e,"namespace").asStr()!=text(args,"namespace",32))continue;
            V row=V::makeObj();for(const char* key:{"namespace","name","program_sha256","values","owned"})row.set(key,get(e,key));row.set("parameters",get(get(e,"program"),"parameters"));row.set("parameter_kinds",parameterKinds(get(e,"program")));row.set("local_edits",V::makeBool(localEdits(root,e)));
            if(get(args,"include_program").b)row.set("program",get(e,"program"));rows.arr.push_back(row);}
        out.set("programs",rows);return true;
    }
    if(action!="prepare")fail("ACTION","Use validate, examples, inspect, prepare or export");expected(s,args);
    const auto mode=text(args,"mode");if(mode!="install"&&mode!="update"&&mode!="configure"&&mode!="remove"&&mode!="detach")fail("ACTION","Prepare install, update, configure, remove or detach");
    const auto ns=text(args,"namespace",32);nameSpace(ns);const size_t index=entryIndex(s.document,ns);const bool installed=index!=s.document.parametricPrograms.arr.size();
    if(mode=="install"?installed:!installed)fail("NAMESPACE",mode=="install"?"Namespace already has a program; use update, configure, remove or detach":"No program is installed in namespace "+ns);
    if(mode=="install"&&(args.find("include_program")))fail("ACTION","include_program is an inspect option");
    if(args.find("values")&&get(args,"values").t!=V::Obj)fail("VALUES","values must map parameter names to numbers, booleans or choice text");
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
    if(request.find("program_path"))out.set("program_path",V::makeStr(utf8(programFile(request,"program_path"))));
    out.set("publication",V::makeStr("doc_apply candidate_id + expected_revision"));return true;
}

void appendParametricProgramTools(V& list){
    V tool=V::makeObj(),schema,annotations=V::makeObj();tool.set("name",V::makeStr("parametric_program"));
    tool.set("description",V::makeStr("Code-defined parametric design (ark.parametric-program.v1): number, boolean, choice and text-choice parameters, let/set/for/if/assert/call statements and emit templates compile deterministically into native feature.add, sketch.set, plane.set and material.set commands under one namespace. validate compiles without the document; examples lists starter programs; inspect reads installed bindings and local edits; prepare install/update/configure/remove/detach evaluates one isolated exact candidate for explicit doc_apply. export{namespace,path,expected_revision,overwrite?} writes the installed source with its SHA-256 to a new .arkprog file; validate/prepare accept program_path to read one. Re-runs replace only namespace-owned objects and reject local edits or outside dependants. Programs never run files, network or scripts."));
    arkjson::parse(R"({"type":"object","properties":{"action":{"type":"string","enum":["validate","examples","inspect","prepare","export"]},"mode":{"type":"string","enum":["install","update","configure","remove","detach"]},"namespace":{"type":"string"},"program":{"type":"object"},"program_path":{"type":"string","description":"validate/prepare: absolute path of a .arkprog file (at most 256 KiB, digest checked) read instead of program"},"values":{"type":"object","additionalProperties":{"type":["number","boolean","string"]}},"include_program":{"type":"boolean"},"path":{"type":"string","description":"export: absolute path of the new .arkprog file"},"overwrite":{"type":"boolean","description":"export: replace an existing .arkprog file; default false"},"expected_revision":{"type":"integer","minimum":0}},"required":["action"],"additionalProperties":false})",schema);
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
    add("turned_knob","Turned knob: a half profile of line, cubic and arc wire segments revolved into one exact solid with smooth curved faces.",R"JSON({"format":"ark.parametric-program.v1","name":"Turned knob","description":"Half profile on XZ (local x radial, local y along world Z) revolved about its local Y axis.",
        "parameters":{"height":{"default":60,"min":30,"max":200,"unit":"mm"},"base_radius":{"default":22,"min":10,"max":80,"unit":"mm"},"waist_radius":{"default":9,"min":4,"max":60,"unit":"mm"},"head_radius":{"default":15,"min":6,"max":70,"unit":"mm"},"foot":{"default":5,"min":2,"max":20,"unit":"mm"}},
        "body":[{"assert":"waist_radius+2<=min(base_radius,head_radius)","message":"The waist must be at least 2 mm narrower than the base and the head"},{"assert":"foot+head_radius+10<=height","message":"Height must fit the foot, a 10 mm neck and the domed head"},
            {"let":"neck","value":"foot+(height-foot-head_radius)/2"},{"let":"shoulder","value":"height-head_radius"},
            {"emit":{"op":"sketch.set","name":"profile","sketch":{"plane":"XZ","entities":[{"kind":"wire","name":"half","start":["0","0"],"segments":[{"kind":"line","to":["{base_radius}","0"]},{"kind":"line","to":["{base_radius}","{foot}"]},
                {"kind":"cubic","c1":["{base_radius}","{(foot+neck)/2}"],"c2":["{waist_radius}","{(foot+neck)/2}"],"to":["{waist_radius}","{neck}"]},{"kind":"cubic","c1":["{waist_radius}","{(neck+shoulder)/2}"],"c2":["{head_radius}","{(neck+shoulder)/2}"],"to":["{head_radius}","{shoulder}"]},
                {"kind":"arc","to":["0","{height}"],"radius":"{head_radius}","sweep":-1},{"kind":"line","to":["0","0"]}]}]}}},
            {"emit":{"op":"feature.add","type":"revolve_surface","name":"Knob","p":{"sketch":"profile","angle":360,"solid":1},"native":{"axis":"Y"}}}]})JSON");
    return out;
}
}

// Platform rename for .arkprog publication, kept after all program code so the
// system headers' macros cannot reach it.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace ark { namespace {
bool publishProgramFile(const std::filesystem::path& staged,const std::filesystem::path& target,bool replace){
#ifdef _WIN32
    return MoveFileExW(staged.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0))!=0;
#else
    std::error_code ec;if(replace){std::filesystem::rename(staged,target,ec);return !ec;}
    if(::link(staged.c_str(),target.c_str())!=0)return false;std::filesystem::remove(staged,ec);return true;
#endif
}
} }
