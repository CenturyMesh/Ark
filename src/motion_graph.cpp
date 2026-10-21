#include "ark/motion_graph.hpp"
#include "ark/exploded_view.hpp"
#include "ark/agent.hpp"
#include "ark/assembly_io.hpp"
#include "ark/iteration_history.hpp"
#include "ark/native_backend.hpp"
#include "ark/native_features.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <mutex>
#include <set>
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
constexpr double kMaxDuration=600,kMaxFps=240,kRevoluteTolerance=1e-4,kPrismaticTolerance=1e-5;
constexpr size_t kMaxMotions=32,kMaxChannels=32,kMaxNodes=512,kMaxDepth=16,kMaxSeams=4096,kMaxFrames=7201,kMaxInstanceFrames=16384,kMaxGraphBytes=262144,kMaxBakeBytes=2u*1024u*1024u,kMaxQueryFrames=600;
struct Sample {double value=0,slope=0;};
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
std::string num(double value){char buf[48];std::snprintf(buf,sizeof buf,"%.6g",value);return buf;}
void keys(const V& v,std::initializer_list<const char*> allowed,const std::string& what){need(v.t==V::Obj,"MOTION_INVALID_GRAPH: "+what+" must be an object");for(const auto& p:v.obj){bool found=false;for(auto key:allowed)found|=p.first==key;need(found,"MOTION_INVALID_GRAPH: unknown "+what+" field: "+p.first);}}
std::string text(const V& v,const char* key,size_t limit=128){const auto* p=v.find(key);need(p&&p->t==V::Str&&!p->str.empty()&&p->str.size()<=limit,std::string("MOTION_INVALID_GRAPH: ")+key+" requires 1.."+std::to_string(limit)+" characters");return p->str;}
bool identifier(const std::string& s){if(s.empty()||s.size()>64||!(std::isalpha((unsigned char)s[0])||s[0]=='_'))return false;for(char c:s)if(!(std::isalnum((unsigned char)c)||c=='_'))return false;return true;}
// Variable identifiers of an expression; function names and numeric literals are skipped.
std::vector<std::string> identifiers(const std::string& s){std::vector<std::string> out;for(size_t i=0;i<s.size();){const char c=s[i];
    if(std::isdigit((unsigned char)c)||c=='.'){++i;while(i<s.size()&&(std::isalnum((unsigned char)s[i])||s[i]=='.'||((s[i]=='+'||s[i]=='-')&&(s[i-1]=='e'||s[i-1]=='E'))))++i;continue;}
    if(std::isalpha((unsigned char)c)||c=='_'){size_t j=i;while(j<s.size()&&(std::isalnum((unsigned char)s[j])||s[j]=='_'))++j;size_t k=j;while(k<s.size()&&std::isspace((unsigned char)s[k]))++k;if(k>=s.size()||s[k]!='(')out.push_back(s.substr(i,j-i));i=j;continue;}
    ++i;}return out;}
void canonical(V& value){if(value.t==V::Obj){for(auto& item:value.obj)canonical(item.second);std::sort(value.obj.begin(),value.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(value.t==V::Arr)for(auto& item:value.arr)canonical(item);}
const char* kBase64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string encodeFloats(const std::vector<float>& values){
    std::string bytes;bytes.reserve(values.size()*4);for(float f:values){uint32_t u;std::memcpy(&u,&f,4);for(int k=0;k<4;++k)bytes+=(char)((u>>(8*k))&255u);}
    std::string out;out.reserve((bytes.size()+2)/3*4);for(size_t i=0;i<bytes.size();i+=3){uint32_t x=(uint32_t)(unsigned char)bytes[i]<<16;if(i+1<bytes.size())x|=(uint32_t)(unsigned char)bytes[i+1]<<8;if(i+2<bytes.size())x|=(unsigned char)bytes[i+2];
        out+=kBase64[(x>>18)&63];out+=kBase64[(x>>12)&63];out+=i+1<bytes.size()?kBase64[(x>>6)&63]:'=';out+=i+2<bytes.size()?kBase64[x&63]:'=';}return out;
}
std::vector<float> decodeFloats(const std::string& textValue,size_t expected,const char* what){
    need(textValue.size()%4==0&&textValue.size()/4*3>=expected*4&&textValue.size()/4*3<expected*4+3,std::string("MOTION_INVALID_BAKE: ")+what+" size does not match its declared frames");
    int table[256];std::fill(std::begin(table),std::end(table),-1);for(int i=0;i<64;++i)table[(unsigned char)kBase64[i]]=i;
    std::vector<unsigned char> bytes;bytes.reserve(expected*4);
    for(size_t i=0;i<textValue.size();i+=4){int q[4];int pad=0;for(int k=0;k<4;++k){const unsigned char c=(unsigned char)textValue[i+k];if(c=='='){need(i+4==textValue.size()&&k>=2,"MOTION_INVALID_BAKE: invalid base64 padding");q[k]=0;++pad;}else{need(!pad&&table[c]>=0,"MOTION_INVALID_BAKE: invalid base64 payload");q[k]=table[c];}}
        const uint32_t x=(uint32_t)q[0]<<18|(uint32_t)q[1]<<12|(uint32_t)q[2]<<6|(uint32_t)q[3];bytes.push_back((unsigned char)(x>>16));if(pad<2)bytes.push_back((unsigned char)(x>>8));if(pad<1)bytes.push_back((unsigned char)x);}
    need(bytes.size()==expected*4,std::string("MOTION_INVALID_BAKE: ")+what+" byte count does not match its declared frames");
    std::vector<float> out(expected);for(size_t i=0;i<expected;++i){uint32_t u=0;for(int k=0;k<4;++k)u|=(uint32_t)bytes[i*4+k]<<(8*k);float f;std::memcpy(&f,&u,4);need(std::isfinite(f),std::string("MOTION_INVALID_BAKE: nonfinite ")+what);out[i]=f;}return out;
}
Sample sampleNode(const CompiledMotion& m,int index,double t,int side){
    const auto& n=m.nodes[(size_t)index];const double d=n.duration;t=std::clamp(t,0.,d);const auto& type=n.type;
    if(type=="hold")return {n.start,0};
    if(type=="linear"){const double rate=(n.p[0]-n.start)/d;return {n.start+rate*t,rate};}
    if(type=="ease"){const double u=t/d,delta=n.p[0]-n.start;double f=0,fp=0;if(n.ease=="in"){f=u*u*u;fp=3*u*u;}else if(n.ease=="out"){const double w=1-u;f=1-w*w*w;fp=3*w*w;}else{f=u*u*(3-2*u);fp=6*u*(1-u);}return {n.start+delta*f,delta*fp/d};}
    if(type=="bezier"){
        const double t1=n.p[0],v1=n.p[1],t2=n.p[2],v2=n.p[3],y3=n.p[4],y0=n.start;
        auto bx=[&](double s){const double w=1-s;return 3*w*w*s*t1+3*w*s*s*t2+s*s*s*d;};auto bdx=[&](double s){const double w=1-s;return 3*w*w*t1+6*w*s*(t2-t1)+3*s*s*(d-t2);};
        double lo=0,hi=1,s=t/d;for(int i=0;i<64;++i){const double x=bx(s)-t;if(std::fabs(x)<=1e-13*std::max(1.,d))break;if(x>0)hi=s;else lo=s;const double dx=bdx(s);double next=dx>0?s-x/dx:.5*(lo+hi);if(!(next>lo&&next<hi))next=.5*(lo+hi);s=next;}
        const double w=1-s,y=w*w*w*y0+3*w*w*s*v1+3*w*s*s*v2+s*s*s*y3,dy=3*w*w*(v1-y0)+6*w*s*(v2-v1)+3*s*s*(y3-v2);return {y,dy/bdx(s)};
    }
    if(type=="sine"){const double w=2*K_PI/n.p[2],a=w*t+n.p[3];return {n.p[0]+n.p[1]*std::sin(a),n.p[1]*w*std::cos(a)};}
    if(type=="spring"){
        const double to=n.p[0],w=n.p[1],z=n.p[2],a=n.start-to;double h=0,hp=0;
        if(z<1-1e-9){const double r=std::sqrt(1-z*z),wd=w*r,e=std::exp(-z*w*t);h=e*(std::cos(wd*t)+z/r*std::sin(wd*t));hp=-e*w/r*std::sin(wd*t);}
        else if(z<=1+1e-9){const double e=std::exp(-w*t);h=(1+w*t)*e;hp=-w*w*t*e;}
        else{const double r=std::sqrt(z*z-1),r1=-w*(z-r),r2=-w*(z+r),e1=std::exp(r1*t),e2=std::exp(r2*t);h=(r2*e1-r1*e2)/(r2-r1);hp=r1*r2*(e1-e2)/(r2-r1);}
        return {to+a*h,a*hp};
    }
    if(type=="function"){
        ExprEnv env=m.env;auto at=[&](double x){env.vars["t"]=x;env.vars["u"]=x/d;env.vars["from"]=n.start;bool ok=false;const double y=evalExpr(n.expression,env,&ok);need(ok&&std::isfinite(y),"MOTION_INVALID_EXPRESSION: function "+n.expression+" at local t="+num(x));return y;};
        const double h=std::min(1e-4,d*1e-3);double lo=std::max(0.,t-h),hi=std::min(d,t+h);if(side<0&&t>0)hi=t;else if(side>0&&t<d)lo=t;return {at(t),(at(hi)-at(lo))/(hi-lo)};
    }
    if(type=="sequence"){size_t pick=n.children.size()-1;for(size_t i=0;i<n.children.size();++i){const double a=n.offsets[i],b=a+m.nodes[(size_t)n.children[i]].duration;if(side<0?((t>a||i==0)&&t<=b):(t>=a&&t<b)){pick=i;break;}}return sampleNode(m,n.children[pick],t-n.offsets[pick],side);}
    if(type=="parallel"){Sample out;bool first=true;for(int child:n.children){const auto& c=m.nodes[(size_t)child];const Sample x=t<c.duration||(t==c.duration&&side<0)?sampleNode(m,child,t,side):Sample{c.end,0};
            if(first){out=x;first=false;}else if(n.combine=="add"){out.value+=x.value;out.slope+=x.slope;}else if(n.combine=="min"?x.value<out.value:x.value>out.value)out=x;}return out;}
    if(type=="delay"){if(t<n.p[0]||(t==n.p[0]&&side<0))return {sampleNode(m,n.children[0],0,1).value,0};return sampleNode(m,n.children[0],t-n.p[0],side);}
    if(type=="repeat"||type=="ping_pong"){
        const auto& c=m.nodes[(size_t)n.children[0]];const double period=c.duration*(type=="ping_pong"?2:1),q=t/period;long k=side<0?(long)std::ceil(q-1e-12)-1:(long)std::floor(q+1e-12);k=std::clamp<long>(k,0,n.count-1);
        const double local=std::clamp(t-k*period,0.,period);if(type=="repeat"||local<c.duration||(local==c.duration&&side<0))return sampleNode(m,n.children[0],local,side);
        const auto s=sampleNode(m,n.children[0],period-local,-side);return {s.value,-s.slope};
    }
    if(type=="reverse"){const auto s=sampleNode(m,n.children[0],d-t,-side);return {s.value,-s.slope};}
    if(type=="time_scale"){const auto s=sampleNode(m,n.children[0],t*n.p[0],side);return {s.value,s.slope*n.p[0]};}
    if(type=="offset"){const auto s=sampleNode(m,n.children[0],t,side);return {n.p[1]*s.value+n.p[0],n.p[1]*s.slope};}
    throw std::runtime_error("MOTION_INVALID_GRAPH: unknown compiled node "+type);
}
struct Compiler {
    const ExprEnv& env;CompiledMotion& out;
    double value(const V& v,const std::string& key){
        if(v.t==V::Num){need(std::isfinite(v.num)&&std::fabs(v.num)<=1e9,"MOTION_INVALID_GRAPH: "+key+" must be finite within 1e9");return v.num;}
        need(v.t==V::Str&&!v.str.empty()&&v.str.size()<=2048,"MOTION_INVALID_GRAPH: "+key+" must be a finite number or parameter expression");
        bool ok=false;const double x=evalExpr(v.str,env,&ok);need(ok&&std::isfinite(x)&&std::fabs(x)<=1e9,"MOTION_INVALID_EXPRESSION: "+key+" = "+v.str);return x;
    }
    double number(const V& v,const char* key){const auto* p=v.find(key);need(p,std::string("MOTION_INVALID_GRAPH: missing ")+key);return value(*p,key);}
    double optional(const V& v,const char* key,double fallback){const auto* p=v.find(key);return p?value(*p,key):fallback;}
    double positive(const V& v,const char* key,double maximum){const double x=number(v,key);need(x>0&&x<=maximum,std::string("MOTION_INVALID_GRAPH: ")+key+" must be in (0,"+num(maximum)+"]");return x;}
    int node(const V& v,double inherited,size_t depth){
        need(depth<=kMaxDepth,"MOTION_GRAPH_CAPACITY: node nesting exceeds 16 levels");need(out.nodes.size()<kMaxNodes,"MOTION_GRAPH_CAPACITY: graph exceeds 512 nodes");
        need(v.t==V::Obj,"MOTION_INVALID_GRAPH: curve node must be an object");MotionCurveNode n;n.type=text(v,"type",32);const auto& type=n.type;
        auto start=[&]{const auto* p=v.find("from");return p?value(*p,"from"):inherited;};
        auto item=[&](double carry){const auto* p=v.find("item");need(p,"MOTION_INVALID_GRAPH: "+type+" requires item");const int child=node(*p,carry,depth+1);n.children={child};return out.nodes[(size_t)child].duration;};
        if(type=="hold"){keys(v,{"type","duration","from","value"},"hold");need(!(v.find("value")&&v.find("from")),"MOTION_INVALID_GRAPH: hold accepts value or from, not both");n.duration=positive(v,"duration",kMaxDuration);n.start=v.find("value")?number(v,"value"):start();}
        else if(type=="linear"||type=="ease"){keys(v,{"type","duration","from","to","ease"},type);n.duration=positive(v,"duration",kMaxDuration);n.start=start();n.p[0]=number(v,"to");
            if(type=="ease"){n.ease=v.find("ease")?text(v,"ease",16):"in_out";need(n.ease=="in"||n.ease=="out"||n.ease=="in_out","MOTION_INVALID_GRAPH: ease must be in, out or in_out");}else need(!v.find("ease"),"MOTION_INVALID_GRAPH: unknown linear field: ease");}
        else if(type=="bezier"){keys(v,{"type","duration","from","to","c1","c2"},"bezier");n.duration=positive(v,"duration",kMaxDuration);n.start=start();n.p[4]=number(v,"to");
            for(int k=0;k<2;++k){const char* key=k?"c2":"c1";const auto* c=v.find(key);need(c&&c->t==V::Arr&&c->arr.size()==2,std::string("MOTION_INVALID_GRAPH: ")+key+" must be [time_s,value]");n.p[2*k]=value(c->arr[0],key);n.p[2*k+1]=value(c->arr[1],key);
                need(n.p[2*k]>0&&n.p[2*k]<n.duration,std::string("MOTION_BEZIER_NOT_MONOTONIC: ")+key+" time must be strictly inside (0,duration) so value remains a single-valued function of time");}}
        else if(type=="sine"){keys(v,{"type","duration","from","center","amplitude","period_s","phase_deg"},"sine");n.duration=positive(v,"duration",kMaxDuration);n.start=start();n.p[0]=optional(v,"center",n.start);n.p[1]=number(v,"amplitude");n.p[2]=positive(v,"period_s",6000);n.p[3]=deg2rad(optional(v,"phase_deg",0));need(std::fabs(n.p[1])<=1e6,"MOTION_INVALID_GRAPH: amplitude exceeds 1e6");}
        else if(type=="spring"){keys(v,{"type","duration","from","to","frequency_hz","damping"},"spring");n.duration=positive(v,"duration",kMaxDuration);n.start=start();n.p[0]=number(v,"to");n.p[1]=2*K_PI*positive(v,"frequency_hz",100);n.p[2]=optional(v,"damping",.3);need(n.p[2]>=0&&n.p[2]<=10,"MOTION_INVALID_GRAPH: damping must be in [0,10]");}
        else if(type=="function"){keys(v,{"type","duration","from","expr"},"function");n.duration=positive(v,"duration",kMaxDuration);n.start=start();n.expression=text(v,"expr",2048);}
        else if(type=="sequence"||type=="parallel"){
            if(type=="sequence")keys(v,{"type","items","from"},type);else keys(v,{"type","items","from","combine"},type);
            const auto* items=v.find("items");need(items&&items->t==V::Arr&&!items->arr.empty()&&items->arr.size()<=256,"MOTION_INVALID_GRAPH: "+type+" requires 1..256 items");n.start=start();
            if(type=="parallel"){n.combine=v.find("combine")?text(v,"combine",8):"add";need(n.combine=="add"||n.combine=="min"||n.combine=="max","MOTION_INVALID_GRAPH: combine must be add, min or max");}
            double cursor=0,carry=n.start;for(size_t i=0;i<items->arr.size();++i){const double inherit=type=="sequence"?carry:(n.combine=="add"&&i>0?0.:n.start);const int child=node(items->arr[i],inherit,depth+1);n.children.push_back(child);n.offsets.push_back(type=="sequence"?cursor:0);
                const auto& c=out.nodes[(size_t)child];if(type=="sequence"){cursor+=c.duration;carry=c.end;}else cursor=std::max(cursor,c.duration);}
            n.duration=cursor;
        }
        else if(type=="delay"){keys(v,{"type","seconds","item","from"},"delay");n.p[0]=number(v,"seconds");need(n.p[0]>0&&n.p[0]<=kMaxDuration,"MOTION_INVALID_GRAPH: delay seconds must be in (0,600]");n.start=start();n.duration=n.p[0]+item(n.start);}
        else if(type=="repeat"||type=="ping_pong"){keys(v,{"type","count","item","from"},type);const double count=optional(v,"count",type=="repeat"?2:1);need(count>=1&&count<=1000&&count==std::floor(count),"MOTION_INVALID_GRAPH: count must be an integer 1..1000");n.count=(int)count;n.start=start();n.duration=item(n.start)*n.count*(type=="ping_pong"?2:1);}
        else if(type=="reverse"){keys(v,{"type","item","from"},"reverse");n.start=start();n.duration=item(n.start);}
        else if(type=="time_scale"){keys(v,{"type","item","factor","duration","from"},"time_scale");need((v.find("factor")!=nullptr)!=(v.find("duration")!=nullptr),"MOTION_INVALID_GRAPH: time_scale requires exactly one of factor or duration");n.start=start();const double inner=item(n.start);
            n.p[0]=v.find("factor")?number(v,"factor"):inner/positive(v,"duration",kMaxDuration);need(n.p[0]>=1e-3&&n.p[0]<=1e3,"MOTION_INVALID_GRAPH: time_scale factor must be in [0.001,1000]");n.duration=inner/n.p[0];}
        else if(type=="offset"){keys(v,{"type","item","value","gain","from"},"offset");n.start=start();n.p[0]=optional(v,"value",0);n.p[1]=optional(v,"gain",1);n.duration=item(n.start);}
        else throw std::runtime_error("MOTION_INVALID_GRAPH: unknown node type "+type+"; use hold, linear, ease, bezier, sine, spring, function, sequence, parallel, delay, repeat, ping_pong, reverse, time_scale or offset");
        need(n.duration>0&&n.duration<=kMaxDuration,"MOTION_INVALID_GRAPH: "+type+" duration must be in (0,600] s");
        out.nodes.push_back(std::move(n));const int index=(int)out.nodes.size()-1;
        const double end=sampleNode(out,index,out.nodes[(size_t)index].duration,-1).value;need(std::isfinite(end),"MOTION_NONFINITE: "+type+" segment end is not finite");out.nodes[(size_t)index].end=end;return index;
    }
};
std::vector<double> localSeams(const CompiledMotion& m,int index){
    const auto& n=m.nodes[(size_t)index];std::vector<double> out;
    auto push=[&](double x){out.push_back(x);need(out.size()<=kMaxSeams,"MOTION_GRAPH_CAPACITY: graph has more than 4096 segment boundaries");};
    auto append=[&](const std::vector<double>& seams,double origin,double sign){for(double x:seams)push(origin+sign*x);};
    if(n.type=="sequence")for(size_t i=0;i<n.children.size();++i){if(i)push(n.offsets[i]);append(localSeams(m,n.children[i]),n.offsets[i],1);}
    else if(n.type=="parallel")for(int child:n.children){append(localSeams(m,child),0,1);if(m.nodes[(size_t)child].duration<n.duration)push(m.nodes[(size_t)child].duration);}
    else if(n.type=="delay"){push(n.p[0]);append(localSeams(m,n.children[0]),n.p[0],1);}
    else if(n.type=="repeat"){const auto seams=localSeams(m,n.children[0]);const double d=m.nodes[(size_t)n.children[0]].duration;for(int k=0;k<n.count;++k){if(k)push(k*d);append(seams,k*d,1);}}
    else if(n.type=="ping_pong"){const auto seams=localSeams(m,n.children[0]);const double d=m.nodes[(size_t)n.children[0]].duration;for(int k=0;k<n.count;++k){const double base=2*d*k;if(k)push(base);append(seams,base,1);push(base+d);append(seams,base+2*d,-1);}}
    else if(n.type=="reverse")append(localSeams(m,n.children[0]),n.duration,-1);
    else if(n.type=="time_scale")for(double x:localSeams(m,n.children[0]))push(x/n.p[0]);
    else if(n.type=="offset")append(localSeams(m,n.children[0]),0,1);
    return out;
}
void bezierHandles(const CompiledMotion& m,int index,double origin,double scale,double gain,double add,std::vector<MotionBezierHandle>& out){
    const auto& n=m.nodes[(size_t)index];if(out.size()>=256)return;
    auto map=[&](double t,double v,double& gt,double& gv){gt=origin+scale*t;gv=gain*v+add;};
    if(n.type=="bezier"){MotionBezierHandle h;map(0,n.start,h.t0,h.v0);map(n.p[0],n.p[1],h.t1,h.v1);map(n.p[2],n.p[3],h.t2,h.v2);map(n.duration,n.p[4],h.t3,h.v3);out.push_back(h);}
    else if(n.type=="sequence")for(size_t i=0;i<n.children.size();++i)bezierHandles(m,n.children[i],origin+scale*n.offsets[i],scale,gain,add,out);
    else if(n.type=="delay")bezierHandles(m,n.children[0],origin+scale*n.p[0],scale,gain,add,out);
    else if(n.type=="repeat"){const double d=m.nodes[(size_t)n.children[0]].duration;for(int k=0;k<n.count&&out.size()<256;++k)bezierHandles(m,n.children[0],origin+scale*k*d,scale,gain,add,out);}
    else if(n.type=="time_scale")bezierHandles(m,n.children[0],origin,scale/n.p[0],gain,add,out);
    else if(n.type=="offset")bezierHandles(m,n.children[0],origin,scale,gain*n.p[1],gain*n.p[0]+add,out);
}
void applyPose(Document& d,const V& commands){
    for(const auto& c:commands.arr){const auto* id=c.find("id");need(id&&id->t==V::Num,"MOTION_BAKE_REJECTED: invalid pose recipe");const u64 key=(u64)id->num;
        auto node=std::find_if(d.assembly.begin(),d.assembly.end(),[&](const AssemblyNode& n){return n.id==key;});need(node!=d.assembly.end(),"MOTION_BAKE_REJECTED: pose recipe names a missing occurrence");
        if(const auto* p=c.find("p"))for(const auto& kv:p->obj)node->p[kv.first]=kv.second.dump();
        if(const auto* pattern=c.find("pattern"))readAssemblyPattern(node->pattern,*pattern);}
}
struct GraphCheck {AssemblyManipulationProgram program;std::vector<MotionJointBinding> bindings;CompiledMotion compiled;V diagnostics;};
GraphCheck checkGraph(const Document& d,const V& graph){
    GraphCheck g;std::string error;need(graph.t==V::Obj,"MOTION_INVALID_GRAPH: graph must be an object");need(graph.dump().size()<=kMaxGraphBytes,"MOTION_GRAPH_CAPACITY: graph exceeds 256 KiB");
    need(buildAssemblyManipulation(d,g.program,error),"MOTION_JOINT_NOT_DRIVABLE: assembly solve failed: "+error);
    need(bindMotionJoints(g.program,d,graph,g.bindings,error),error);
    std::map<std::string,double> starts;for(const auto& b:g.bindings)if(b.mate)starts[b.channel]=b.position;
    need(compileMotionGraph(graph,d.buildEnv(),starts,g.compiled,error),error);
    std::vector<MotionLimit> limits;for(const auto& b:g.bindings)limits.push_back({b.limited,b.min,b.max});
    g.diagnostics=motionDiagnostics(g.compiled,limits);
    V* rows=nullptr;for(auto& item:g.diagnostics.obj)if(item.first=="channels")rows=&item.second;need(rows&&rows->arr.size()==g.bindings.size(),"MOTION_INVALID_GRAPH: diagnostics do not match channels");
    for(size_t c=0;c<g.bindings.size();++c){auto& row=rows->arr[c];const auto& b=g.bindings[c];row.obj.insert(row.obj.begin()+1,{"mate",b.mate?V::makeNum((double)b.mate):V{}});if(b.mate){row.set("joint_type",V::makeStr(b.type));row.set("unit",V::makeStr(b.unit));row.set("current",V::makeNum(b.position));row.set("driver",V::makeNum((double)b.driver));row.set("solver",V::makeStr(b.constrained?"constrained_continuation":"rigid_side"));}else if(!b.explodeView.empty()){row.set("unit",V::makeStr("factor"));row.set("explode_view",V::makeStr(b.explodeView));}else row.set("unit",V::makeStr("signal"));}
    for(size_t c=0;c<g.bindings.size();++c)if(!g.bindings[c].explodeView.empty()){const double lo=rows->arr[c].find("min")->num,hi=rows->arr[c].find("max")->num;need(lo>=-1e-9&&hi<=1+1e-9,"MOTION_EXPLODE_RANGE: channel "+g.bindings[c].channel+" spans ["+num(lo)+", "+num(hi)+"]; an explode_view factor stays within [0,1]");}
    if(const auto* violation=g.diagnostics.find("limit_violation"))throw std::runtime_error("MOTION_LIMIT_VIOLATION: "+violation->find("message")->str);
    if(!g.compiled.allowJumps)if(const auto* jump=g.diagnostics.find("discontinuity"))throw std::runtime_error("MOTION_DISCONTINUITY: "+jump->find("message")->str+"; make the segments meet or set allow_jumps:true");
    return g;
}
V encodeBake(const CompiledMotion& m,const std::vector<MotionJointBinding>& bindings,const MotionSolvedFrames& f,double fps,u64 revision,const std::string& digest,const std::string& graphDigest){
    std::vector<size_t> moving;
    for(size_t i=0;i<f.baseline.size();++i){bool moved=false;for(const auto& frame:f.poses){for(int e=0;e<16&&!moved;++e)moved=std::fabs(frame[i].m[e]-f.baseline[i].world.m[e])>1e-9;if(moved)break;}if(moved)moving.push_back(i);}
    need(moving.size()*f.poses.size()<=kMaxInstanceFrames,"MOTION_BAKE_CAPACITY: "+std::to_string(moving.size()*f.poses.size())+" moving instance-frames exceed 16384; reduce fps, duration or moving occurrences");
    std::vector<float> poses,coordinates;poses.reserve(moving.size()*f.poses.size()*12);
    for(const auto& frame:f.poses)for(size_t i:moving)for(int e:{0,1,2,4,5,6,8,9,10,12,13,14})poses.push_back((float)frame[i].m[e]);
    for(const auto& frame:f.coordinates)for(double value:frame)coordinates.push_back((float)value);
    V bake=V::makeObj(),instances=V::makeArr(),channels=V::makeArr(),solver=V::makeObj();
    bake.set("format",V::makeStr("ark.baked_motion.v1"));bake.set("fps",V::makeNum(fps));bake.set("frame_count",V::makeNum((double)f.times.size()));bake.set("duration_s",V::makeNum(m.duration));
    bake.set("source_revision",V::makeNum((double)revision));bake.set("source_digest",V::makeStr(digest));bake.set("graph_digest",V::makeStr(graphDigest));
    for(size_t i:moving){V row=V::makeObj();row.set("node",V::makeNum((double)f.baseline[i].node));row.set("copy",V::makeNum(f.baseline[i].copy));row.set("name",V::makeStr(f.baseline[i].name));instances.arr.push_back(row);}
    for(size_t c=0;c<bindings.size();++c){const auto& b=bindings[c];V row=V::makeObj();row.set("name",V::makeStr(m.channels[c].name));if(b.mate){row.set("mate",V::makeNum((double)b.mate));row.set("unit",V::makeStr(b.unit));}else if(!b.explodeView.empty()){row.set("explode_view",V::makeStr(b.explodeView));row.set("unit",V::makeStr("factor"));}else row.set("unit",V::makeStr("signal"));row.set("start",V::makeNum(f.coordinates.empty()?m.channels[c].start:f.coordinates.front()[c]));
        row.set("graph_start",V::makeNum(m.channels[c].start));if(b.limited){row.set("min",V::makeNum(b.min));row.set("max",V::makeNum(b.max));}channels.arr.push_back(row);}
    bake.set("instances",instances);bake.set("channels",channels);bake.set("frames_f32le_base64",V::makeStr(encodeFloats(poses)));bake.set("coordinates_f32le_base64",V::makeStr(encodeFloats(coordinates)));
    solver.set("rigid_side_steps",V::makeNum((double)f.rigidSteps));solver.set("constrained_steps",V::makeNum((double)f.constrainedSteps));solver.set("max_continuation_steps",V::makeNum(f.maxContinuationSteps));solver.set("max_channel_error",V::makeNum(f.maxChannelError));
    bake.set("solver",solver);bake.set("clearance_checked",V::makeBool(false));bake.set("dynamics_checked",V::makeBool(false));return bake;
}
V bakeSummary(const V& bake){V out=V::makeObj();for(const auto& item:bake.obj)if(item.first!="frames_f32le_base64"&&item.first!="coordinates_f32le_base64")out.set(item.first,item.second);return out;}
std::string motionName(const V& command,const char* key){const auto value=text(command,key,128);for(unsigned char c:value)need(c>=32,"MOTION_INVALID_GRAPH: motion names cannot contain control characters");return value;}
V motionStatus(const V& motion,const std::string* digest,const std::string& graphError){
    V status=V::makeObj(),reasons=V::makeArr();const V* bake=motion.find("bake");const V* graph=motion.find("graph");
    if(!bake)reasons.arr.push_back(V::makeStr("not_baked"));
    else{if(digest&&bake->find("source_digest")&&bake->find("source_digest")->str!=*digest)reasons.arr.push_back(V::makeStr("assembly_changed"));if(graph&&bake->find("graph_digest")&&bake->find("graph_digest")->str!=motionGraphDigest(*graph))reasons.arr.push_back(V::makeStr("graph_changed"));}
    if(!graphError.empty())reasons.arr.push_back(V::makeStr("graph_invalid"));
    status.set("baked",V::makeBool(bake!=nullptr));status.set("stale",V::makeBool(bake&&!reasons.arr.empty()));status.set("stale_reasons",reasons);if(!digest)status.set("assembly_digest_checked",V::makeBool(false));if(!graphError.empty())status.set("graph_error",V::makeStr(graphError));return status;
}
}

bool compileMotionGraph(const V& graph,const ExprEnv& env,const std::map<std::string,double>& startValues,CompiledMotion& out,std::string& error){
    out={};error.clear();
    try{
        keys(graph,{"channels","duration_s","allow_jumps","description"},"graph");if(const auto* d=graph.find("description"))need(d->t==V::Str&&d->str.size()<=1024,"MOTION_INVALID_GRAPH: description must be text of at most 1024 characters");
        if(const auto* jumps=graph.find("allow_jumps")){need(jumps->t==V::Bool,"MOTION_INVALID_GRAPH: allow_jumps must be boolean");out.allowJumps=jumps->b;}
        const auto* channels=graph.find("channels");need(channels&&channels->t==V::Arr&&!channels->arr.empty()&&channels->arr.size()<=kMaxChannels,"MOTION_INVALID_GRAPH: graph requires 1..32 channels");
        out.env=env;Compiler compiler{out.env,out};std::set<std::string> names;
        for(const auto& c:channels->arr){keys(c,{"name","mate","from","curve","expr","explode_view"},"channel");(void)motionExplodeView(c);MotionChannelPlan plan;plan.name=text(c,"name",64);
            need(identifier(plan.name),"MOTION_INVALID_GRAPH: channel name "+plan.name+" must be an identifier ([A-Za-z_][A-Za-z0-9_]*) usable in coupling expressions");
            need(plan.name!="t"&&plan.name!="u"&&plan.name!="from"&&plan.name!="pi"&&plan.name!="e"&&!env.vars.count(plan.name),"MOTION_INVALID_GRAPH: channel name "+plan.name+" collides with a reserved or parameter name");
            need(names.insert(plan.name).second,"MOTION_INVALID_GRAPH: duplicate channel name "+plan.name);
            if(const auto* mate=c.find("mate")){need(mate->t==V::Num&&mate->num>=1&&mate->num==std::floor(mate->num)&&mate->num<=9007199254740990.,"MOTION_INVALID_GRAPH: channel "+plan.name+" mate must be a numeric mate ID");plan.mate=(u64)mate->num;}
            const auto* curve=c.find("curve");const auto* expr=c.find("expr");need((curve!=nullptr)!=(expr!=nullptr),"MOTION_INVALID_GRAPH: channel "+plan.name+" requires exactly one of curve or expr");
            if(expr){need(!c.find("from"),"MOTION_INVALID_GRAPH: coupled channel "+plan.name+" derives its value from expr; remove from");plan.expression=text(c,"expr",2048);}
            else{if(const auto* from=c.find("from")){plan.start=compiler.value(*from,"from");plan.explicitStart=true;}else{const auto known=startValues.find(plan.name);need(known!=startValues.end()||!plan.mate,"MOTION_START_UNKNOWN: channel "+plan.name+" has no from and no measured joint coordinate");plan.start=known!=startValues.end()?known->second:0;}
                plan.root=compiler.node(*curve,plan.start,1);}
            out.channels.push_back(std::move(plan));}
        for(auto& plan:out.channels)if(!plan.expression.empty())for(const auto& id:identifiers(plan.expression)){const auto found=std::find_if(out.channels.begin(),out.channels.end(),[&](const MotionChannelPlan& p){return p.name==id;});if(found!=out.channels.end()){const size_t index=(size_t)(found-out.channels.begin());if(std::find(plan.dependencies.begin(),plan.dependencies.end(),index)==plan.dependencies.end())plan.dependencies.push_back(index);}}
        std::vector<int> colour(out.channels.size(),0);std::vector<std::string> path;
        std::function<void(size_t)> visit=[&](size_t index){if(colour[index]==2)return;if(colour[index]==1){std::string cycle;for(const auto& name:path)cycle+=name+" -> ";throw std::runtime_error("MOTION_COUPLING_CYCLE: "+cycle+out.channels[index].name);}
            colour[index]=1;path.push_back(out.channels[index].name);for(size_t dep:out.channels[index].dependencies)visit(dep);path.pop_back();colour[index]=2;out.order.push_back(index);};
        for(size_t i=0;i<out.channels.size();++i)visit(i);
        double longest=0;for(const auto& plan:out.channels)if(plan.root>=0)longest=std::max(longest,out.nodes[(size_t)plan.root].duration);need(longest>0,"MOTION_INVALID_GRAPH: at least one channel needs a curve to define time");
        out.duration=longest;if(const auto* duration=graph.find("duration_s")){out.duration=compiler.value(*duration,"duration_s");need(out.duration>0&&out.duration<=kMaxDuration,"MOTION_INVALID_GRAPH: duration_s must be in (0,600]");}
        for(double t:{0.,out.duration})for(double value:evaluateMotion(out,t))need(std::isfinite(value),"MOTION_NONFINITE: channel value is not finite");
        return true;
    }catch(const std::exception& e){error=e.what();out={};return false;}
}
std::vector<double> evaluateMotion(const CompiledMotion& m,double time,std::vector<double>* slopes,int side){
    const double t=std::clamp(time,0.,m.duration);std::vector<double> values(m.channels.size()),rates(m.channels.size());
    for(size_t index:m.order){const auto& c=m.channels[index];
        if(c.root>=0){const auto& root=m.nodes[(size_t)c.root];
            if(t>root.duration||(t==root.duration&&side>0&&root.duration<m.duration)){values[index]=root.end;rates[index]=0;}
            else{const auto s=sampleNode(m,c.root,t,side);values[index]=s.value;rates[index]=s.slope;}
            continue;}
        ExprEnv env=m.env;for(size_t dep:c.dependencies)env.vars[m.channels[dep].name]=values[dep];env.vars["t"]=t;
        auto evaluate=[&]{bool ok=false;const double y=evalExpr(c.expression,env,&ok);need(ok&&std::isfinite(y),"MOTION_INVALID_EXPRESSION: channel "+c.name+" = "+c.expression+" at t="+num(t));return y;};
        const double value=evaluate();double rate=0;
        for(size_t dep:c.dependencies){double& slot=env.vars[m.channels[dep].name];const double x=slot,h=1e-6*std::max(1.,std::fabs(x));slot=x+h;const double up=evaluate();slot=x-h;const double down=evaluate();slot=x;rate+=(up-down)/(2*h)*rates[dep];}
        {double& slot=env.vars["t"];const double h=1e-6;slot=t+h;const double up=evaluate();slot=t-h;const double down=evaluate();slot=t;rate+=(up-down)/(2*h);}
        values[index]=value;rates[index]=rate;}
    if(slopes)*slopes=std::move(rates);return values;
}
std::vector<double> motionSeams(const CompiledMotion& m,size_t channel){
    std::vector<double> out;std::set<size_t> seen;std::function<void(size_t)> collect=[&](size_t index){if(!seen.insert(index).second)return;const auto& c=m.channels[index];
        if(c.root>=0){const double d=m.nodes[(size_t)c.root].duration;for(double x:localSeams(m,c.root))if(x>1e-12&&x<std::min(d,m.duration)-1e-12)out.push_back(x);if(d<m.duration-1e-12)out.push_back(d);}
        for(size_t dep:c.dependencies)collect(dep);};
    collect(channel);std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end(),[](double a,double b){return std::fabs(a-b)<=1e-12;}),out.end());return out;
}
std::vector<MotionBezierHandle> motionBezierHandles(const CompiledMotion& m,size_t channel){std::vector<MotionBezierHandle> out;if(channel<m.channels.size()&&m.channels[channel].root>=0)bezierHandles(m,m.channels[channel].root,0,1,1,0,out);return out;}
arkjson::Value motionDiagnostics(const CompiledMotion& m,const std::vector<MotionLimit>& limits){
    const size_t count=m.channels.size(),samples=std::clamp<size_t>((size_t)std::ceil(m.duration*240)+1,2,20001);
    std::vector<double> lo(count,std::numeric_limits<double>::infinity()),hi(count,-std::numeric_limits<double>::infinity());std::vector<std::string> violation(count);
    auto observe=[&](double t,const std::vector<double>& values){for(size_t c=0;c<count;++c){const double v=values[c];need(std::isfinite(v),"MOTION_NONFINITE: channel "+m.channels[c].name+" at t="+num(t));lo[c]=std::min(lo[c],v);hi[c]=std::max(hi[c],v);
        if(c<limits.size()&&limits[c].limited&&violation[c].empty()){const double roundoff=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::fabs(v),std::fabs(limits[c].min),std::fabs(limits[c].max)});if(v<limits[c].min-roundoff||v>limits[c].max+roundoff)violation[c]="channel "+m.channels[c].name+" reaches "+num(v)+" at t="+num(t)+" s outside joint limits ["+num(limits[c].min)+", "+num(limits[c].max)+"]";}}};
    for(size_t i=0;i<samples;++i){const double t=m.duration*(double)i/(double)(samples-1);observe(t,evaluateMotion(m,t));}
    std::vector<std::vector<double>> seams(count);std::set<double> all;for(size_t c=0;c<count;++c){seams[c]=motionSeams(m,c);all.insert(seams[c].begin(),seams[c].end());}
    V out=V::makeObj(),channels=V::makeArr();std::vector<V> breaks(count,V::makeArr());std::vector<size_t> c0(count),c1(count);V firstJump;
    for(double t:all){std::vector<double> leftSlopes,rightSlopes;const auto left=evaluateMotion(m,t,&leftSlopes,-1),right=evaluateMotion(m,t,&rightSlopes,1);observe(t,left);observe(t,right);
        for(size_t c=0;c<count;++c){if(!std::binary_search(seams[c].begin(),seams[c].end(),t))continue;const double jump=right[c]-left[c],slope=rightSlopes[c]-leftSlopes[c];
            const bool valueBreak=std::fabs(jump)>1e-7*std::max(1.,std::fabs(left[c])),slopeBreak=!valueBreak&&std::fabs(slope)>1e-6*std::max({1.,std::fabs(leftSlopes[c]),std::fabs(rightSlopes[c])});if(!valueBreak&&!slopeBreak)continue;
            (valueBreak?c0[c]:c1[c])++;if(breaks[c].arr.size()<32){V row=V::makeObj();row.set("time_s",V::makeNum(t));row.set("kind",V::makeStr(valueBreak?"C0":"C1"));row.set("value_jump",V::makeNum(jump));row.set("slope_jump",V::makeNum(slope));breaks[c].arr.push_back(row);}
            if(valueBreak&&firstJump.isNull()){firstJump=V::makeObj();firstJump.set("channel",V::makeStr(m.channels[c].name));firstJump.set("time_s",V::makeNum(t));firstJump.set("value_jump",V::makeNum(jump));firstJump.set("message",V::makeStr("channel "+m.channels[c].name+" jumps "+num(jump)+" at t="+num(t)+" s"));}}}
    std::vector<double> startSlopes,endSlopes;const auto first=evaluateMotion(m,0,&startSlopes,1),last=evaluateMotion(m,m.duration,&endSlopes,-1);
    for(size_t c=0;c<count;++c){V row=V::makeObj();const auto& plan=m.channels[c];row.set("name",V::makeStr(plan.name));row.set("kind",V::makeStr(plan.root>=0?"curve":"coupling"));if(plan.root<0)row.set("expr",V::makeStr(plan.expression));
        row.set("start",V::makeNum(first[c]));row.set("end",V::makeNum(last[c]));row.set("min",V::makeNum(lo[c]));row.set("max",V::makeNum(hi[c]));
        row.set("continuity",V::makeStr(c0[c]?"discontinuous":c1[c]?"C0":"C1"));row.set("value_breaks",V::makeNum((double)c0[c]));row.set("slope_breaks",V::makeNum((double)c1[c]));row.set("breaks",breaks[c]);
        const bool loopValue=std::fabs(last[c]-first[c])<=1e-7*std::max(1.,std::fabs(first[c]));const bool loopSlope=std::fabs(endSlopes[c]-startSlopes[c])<=1e-6*std::max({1.,std::fabs(endSlopes[c]),std::fabs(startSlopes[c])});row.set("loop_continuity",V::makeStr(!loopValue?"discontinuous":loopSlope?"C1":"C0"));
        if(c<limits.size()&&limits[c].limited){row.set("limit_min",V::makeNum(limits[c].min));row.set("limit_max",V::makeNum(limits[c].max));}channels.arr.push_back(row);}
    out.set("duration_s",V::makeNum(m.duration));out.set("channels",channels);out.set("samples",V::makeNum((double)samples));out.set("seam_count",V::makeNum((double)all.size()));
    out.set("limit_check",V::makeStr("analytic graph sampled at 240 Hz plus every segment boundary; each baked frame is also checked by the joint solver"));
    for(size_t c=0;c<count;++c)if(!violation[c].empty()){V row=V::makeObj();row.set("channel",V::makeStr(m.channels[c].name));row.set("message",V::makeStr(violation[c]));out.set("limit_violation",row);break;}
    if(!firstJump.isNull())out.set("discontinuity",firstJump);return out;
}
bool bindMotionJoints(const AssemblyManipulationProgram& program,const Document& d,const V& graph,std::vector<MotionJointBinding>& out,std::string& error){
    out.clear();error.clear();
    try{const auto* channels=graph.find("channels");need(graph.t==V::Obj&&channels&&channels->t==V::Arr&&!channels->arr.empty()&&channels->arr.size()<=kMaxChannels,"MOTION_INVALID_GRAPH: graph requires 1..32 channels");std::set<u64> used;std::set<std::string> views;
        for(const auto& c:channels->arr){need(c.t==V::Obj,"MOTION_INVALID_GRAPH: channel must be an object");MotionJointBinding b;b.channel=text(c,"name",64);const auto* mate=c.find("mate");
            if(!mate){b.explodeView=motionExplodeView(c);if(!b.explodeView.empty()){need(findExplodedView(d,b.explodeView),"MOTION_UNKNOWN_EXPLODE_VIEW: channel "+b.channel+" names exploded view "+b.explodeView+", which does not exist");need(views.insert(b.explodeView).second,"MOTION_DUPLICATE_EXPLODE_VIEW: exploded view "+b.explodeView+" is driven by more than one channel");b.type="explode_view";b.unit="factor";}out.push_back(b);continue;}
            need(mate->t==V::Num&&mate->num>=1&&mate->num==std::floor(mate->num)&&mate->num<=9007199254740990.,"MOTION_INVALID_GRAPH: channel "+b.channel+" mate must be a numeric mate ID");b.mate=(u64)mate->num;const std::string label="channel "+b.channel+" mate #"+std::to_string(b.mate);
            const auto found=std::find_if(d.mates.begin(),d.mates.end(),[&](const AssemblyMate& m){return m.id==b.mate;});need(found!=d.mates.end(),"MOTION_UNKNOWN_JOINT: "+label+" does not exist");
            need(found->type=="revolute"||found->type=="prismatic","MOTION_UNSUPPORTED_JOINT: "+label+" has type "+found->type+"; skeleton motion drives revolute (deg) or prismatic (mm) joint coordinates");
            need(used.insert(b.mate).second,"MOTION_DUPLICATE_JOINT: mate #"+std::to_string(b.mate)+" is bound by more than one channel");
            b.type=found->type;b.unit=b.type=="revolute"?"deg":"mm";const std::string mode=b.type=="revolute"?"rotate":"translate";
            const auto joint=std::find_if(program.joints.begin(),program.joints.end(),[&](const AssemblyManipulationJoint& j){return j.mate==b.mate&&j.mode==mode;});
            need(joint!=program.joints.end(),"MOTION_JOINT_NOT_DRIVABLE: "+label+" has no grounded moving side; ground its support with assembly.edit fixed:true");
            AssemblyManipulationJoint selected;std::string reason;need(selectAssemblyManipulationJoint(program,joint->movingEndpoint,b.mate,mode,selected,reason),"MOTION_JOINT_NOT_DRIVABLE: "+label+": "+reason);
            b.driver=joint->movingEndpoint;b.limited=selected.limited;b.min=selected.min;b.max=selected.max;b.position=selected.position;b.constrained=selected.constrained;out.push_back(b);}
        return true;
    }catch(const std::exception& e){error=e.what();out.clear();return false;}
}
std::vector<double> motionFrameTimes(double duration,double fps){
    need(std::isfinite(duration)&&duration>0&&duration<=kMaxDuration,"MOTION_INVALID_BAKE: duration must be in (0,600] s");need(std::isfinite(fps)&&fps>=1&&fps<=kMaxFps,"MOTION_INVALID_BAKE: fps must be in [1,240]");
    const size_t last=(size_t)std::floor(duration*fps+1e-9);need(last+2<=kMaxFrames,"MOTION_BAKE_CAPACITY: more than 7201 frames; reduce fps or duration");
    std::vector<double> times;for(size_t k=0;k<=last;++k)times.push_back(std::min(duration,(double)k/fps));if(times.back()<duration-1e-9)times.push_back(duration);return times;
}
bool solveMotionFrames(Document& work,const CompiledMotion& m,const std::vector<MotionJointBinding>& bindings,const std::vector<double>& times,MotionSolvedFrames& out,std::string& error,bool recordJoints){
    out={};error.clear();
    struct Restore {Document& d;std::vector<AssemblyNode> nodes;~Restore(){d.assembly=std::move(nodes);}} restore{work,work.assembly};
    try{
        need(bindings.size()==m.channels.size(),"MOTION_BAKE_REJECTED: channel bindings do not match the graph");
        AssemblyManipulationProgram program;std::string reason;auto build=[&]{need(buildAssemblyManipulation(work,program,reason),"MOTION_BAKE_REJECTED: assembly solve failed: "+reason);};
        build();out.baseline=program.baseline;out.times=times;std::vector<double> previous;
        for(size_t k=0;k<times.size();++k){
            const double t=times[k];const auto targets=evaluateMotion(m,t);bool dirty=false;const std::string at="frame "+std::to_string(k)+" t="+num(t)+" s";
            for(size_t c=0;c<bindings.size();++c){const auto& b=bindings[c];if(!b.mate)continue;
                if(!previous.empty()&&b.type=="revolute"&&!b.limited)need(std::fabs(targets[c]-previous[c])<180,"MOTION_FRAME_STEP_TOO_LARGE: channel "+b.channel+" turns 180 degrees or more between frames at "+at+"; raise fps");
                if(dirty){build();dirty=false;}
                const std::string mode=b.type=="revolute"?"rotate":"translate";AssemblyManipulationJoint joint;
                need(selectAssemblyManipulationJoint(program,b.driver,b.mate,mode,joint,reason),"MOTION_BAKE_REJECTED: "+at+" channel "+b.channel+": "+reason);
                double change=targets[c]-joint.position;if(b.type=="revolute"&&!joint.limited)change=std::remainder(change,360.);if(std::fabs(change)<=1e-12)continue;
                AssemblyManipulationPreview preview;need(previewAssemblyManipulation(program,b.driver,b.mate,mode,change*joint.coordinateSign,preview,reason),"MOTION_BAKE_REJECTED: "+at+" channel "+b.channel+" (mate #"+std::to_string(b.mate)+"): "+reason);
                (preview.joint.constrained?out.constrainedSteps:out.rigidSteps)++;out.maxContinuationSteps=std::max(out.maxContinuationSteps,preview.continuationSteps);applyPose(work,preview.commands);dirty=true;}
            if(dirty)build();
            std::vector<double> measured(bindings.size());std::vector<MotionJointFrame> solvedJoints(recordJoints?bindings.size():0);
            for(size_t c=0;c<bindings.size();++c){const auto& b=bindings[c];if(!b.mate){measured[c]=targets[c];continue;}const std::string mode=b.type=="revolute"?"rotate":"translate";AssemblyManipulationJoint joint;
                need(selectAssemblyManipulationJoint(program,b.driver,b.mate,mode,joint,reason),"MOTION_BAKE_REJECTED: "+at+" channel "+b.channel+": "+reason);
                if(recordJoints){auto& f=solvedJoints[c];f.origin=joint.origin;f.axis=joint.axis;f.rotate=mode=="rotate";f.constrained=joint.constrained;f.valid=true;const auto known=std::find(out.movingSets.begin(),out.movingSets.end(),joint.movingNodes);f.movingSet=(size_t)(known-out.movingSets.begin());if(known==out.movingSets.end())out.movingSets.push_back(joint.movingNodes);}
                double residual=joint.position-targets[c];if(b.type=="revolute"&&!joint.limited)residual=std::remainder(residual,360.);
                need(std::fabs(residual)<=(b.type=="revolute"?kRevoluteTolerance:kPrismaticTolerance),"MOTION_CHANNEL_CONFLICT: channel "+b.channel+" measures "+num(targets[c]+residual)+" "+b.unit+" after the "+at+" solve, target "+num(targets[c])+"; coupled or shared joints cannot follow independent channels");
                out.maxChannelError=std::max(out.maxChannelError,std::fabs(residual));measured[c]=targets[c]+residual;}
            need(program.baseline.size()==out.baseline.size(),"MOTION_BAKE_REJECTED: occurrence inventory changed during the bake");std::vector<Mat4> poses;poses.reserve(out.baseline.size());
            for(size_t i=0;i<program.baseline.size();++i){need(program.baseline[i].node==out.baseline[i].node&&program.baseline[i].copy==out.baseline[i].copy,"MOTION_BAKE_REJECTED: occurrence order changed during the bake");poses.push_back(program.baseline[i].world);}
            out.poses.push_back(std::move(poses));out.coordinates.push_back(std::move(measured));if(recordJoints)out.joints.push_back(std::move(solvedJoints));previous=targets;
        }
        return true;
    }catch(const std::exception& e){error=e.what();out={};return false;}
}
bool decodeBakedMotion(const V& motion,BakedMotionFrames& out,std::string& error){
    out={};error.clear();
    try{const auto* bake=motion.find("bake");need(bake&&bake->t==V::Obj,"MOTION_NOT_BAKED: motion has no baked frames");
        auto number=[&](const char* key){const auto* p=bake->find(key);need(p&&p->t==V::Num&&std::isfinite(p->num),std::string("MOTION_INVALID_BAKE: missing ")+key);return p->num;};
        out.fps=number("fps");out.duration=number("duration_s");const double frames=number("frame_count");need(out.fps>=1&&out.fps<=kMaxFps&&out.duration>0&&out.duration<=kMaxDuration&&frames>=1&&frames<=(double)kMaxFrames&&frames==std::floor(frames),"MOTION_INVALID_BAKE: fps, duration or frame_count out of range");out.frameCount=(size_t)frames;
        const auto* instances=bake->find("instances");const auto* channels=bake->find("channels");need(instances&&instances->t==V::Arr&&channels&&channels->t==V::Arr&&channels->arr.size()<=kMaxChannels,"MOTION_INVALID_BAKE: instances and channels must be arrays");
        need(instances->arr.size()*out.frameCount<=kMaxInstanceFrames,"MOTION_INVALID_BAKE: instance-frames exceed 16384");
        for(const auto& row:instances->arr){const auto* node=row.find("node");const auto* copy=row.find("copy");need(node&&node->t==V::Num&&node->num>=1&&copy&&copy->t==V::Num&&copy->num>=0,"MOTION_INVALID_BAKE: instance requires node and copy");out.nodes.push_back((u64)node->num);out.copies.push_back((int)copy->num);out.names.push_back(row.find("name")?row.find("name")->asStr():std::string());}
        for(const auto& row:channels->arr){out.channels.push_back(row.find("name")?row.find("name")->asStr():std::string());out.units.push_back(row.find("unit")?row.find("unit")->asStr():std::string());out.starts.push_back(row.find("start")?row.find("start")->asNum():0);out.explodeViews.push_back(row.find("explode_view")?row.find("explode_view")->asStr():std::string());}
        const auto* frames64=bake->find("frames_f32le_base64");const auto* coords64=bake->find("coordinates_f32le_base64");need(frames64&&frames64->t==V::Str&&coords64&&coords64->t==V::Str,"MOTION_INVALID_BAKE: missing frame payloads");
        const auto values=decodeFloats(frames64->str,out.frameCount*out.nodes.size()*12,"frame matrices");out.coordinates=decodeFloats(coords64->str,out.frameCount*out.channels.size(),"joint coordinates");
        out.poses.resize(out.frameCount*out.nodes.size());for(size_t i=0;i<out.poses.size();++i){Mat4 m;const float* v=&values[i*12];const int slots[12]={0,1,2,4,5,6,8,9,10,12,13,14};for(int e=0;e<12;++e)m.m[slots[e]]=v[e];m.m[3]=m.m[7]=m.m[11]=0;m.m[15]=1;out.poses[i]=m;}
        return true;
    }catch(const std::exception& e){error=e.what();out={};return false;}
}
std::string motionSourceDigest(const Document& d,const std::vector<AssemblyPlacement>& placements){
    V root=V::makeObj(),nodes=V::makeArr(),mates=V::makeArr(),params=V::makeArr(),poses=V::makeArr();
    for(const auto& n:d.assembly){V row=V::makeObj(),p=V::makeObj();row.set("id",V::makeNum((double)n.id));row.set("parent",V::makeNum((double)n.parent));row.set("feature",V::makeNum((double)n.feature));row.set("fixed",V::makeBool(n.fixed));for(const auto& kv:n.p)p.set(kv.first,V::makeStr(kv.second));row.set("p",p);if(n.pattern.active)row.set("pattern",assemblyPatternToJson(n.pattern));nodes.arr.push_back(row);}
    for(const auto& m:d.mates)mates.arr.push_back(mateToJson(m));
    for(const auto& p:d.params){V row=V::makeArr();row.arr.push_back(V::makeStr(p.name));row.arr.push_back(V::makeNum(p.value));params.arr.push_back(row);}
    for(const auto& placement:placements){V row=V::makeArr();row.arr.push_back(V::makeNum((double)placement.node));row.arr.push_back(V::makeNum(placement.copy));for(double x:placement.world.m)row.arr.push_back(V::makeNum(std::round(x*1e5)/1e5));poses.arr.push_back(row);}
    root.set("assembly",nodes);root.set("mates",mates);root.set("params",params);root.set("placements",poses);canonical(root);return iterationSha256(root.dump());
}
std::string motionGraphDigest(const V& graph){V copy=graph;canonical(copy);return iterationSha256(copy.dump());}
const V* findMotion(const Document& d,const std::string& name){if(d.motions.t!=V::Arr)return nullptr;for(const auto& row:d.motions.arr)if(const auto* key=row.find("name");key&&key->t==V::Str&&key->str==name)return &row;return nullptr;}
bool motionsRequireDocumentVersion47(const Document& d){return d.motions.t==V::Arr&&!d.motions.arr.empty();}
void validateMotionStorage(const V& motions){
    if(motions.t==V::Null)return;need(motions.t==V::Arr&&!motions.arr.empty()&&motions.arr.size()<=kMaxMotions,"MOTION_CAPACITY: baked motions must be a nonempty array of at most 32 motions");
    std::set<std::string> names;size_t bytes=0;
    for(const auto& row:motions.arr){keys(row,{"name","graph","bake"},"motion");const auto name=motionName(row,"name");need(names.insert(name).second,"MOTION_NAME_CONFLICT: duplicate motion "+name);
        const auto* graph=row.find("graph");need(graph&&graph->t==V::Obj&&graph->find("channels")&&graph->find("channels")->t==V::Arr&&graph->dump().size()<=kMaxGraphBytes,"MOTION_INVALID_GRAPH: motion "+name+" requires a bounded graph object with channels");
        if(row.find("bake")){BakedMotionFrames frames;std::string error;need(decodeBakedMotion(row,frames,error),error);bytes+=row.find("bake")->find("frames_f32le_base64")->str.size();
            for(const char* key:{"source_digest","graph_digest"}){const auto* digest=row.find("bake")->find(key);need(digest&&digest->t==V::Str&&digest->str.size()==64,std::string("MOTION_INVALID_BAKE: missing ")+key);}}}
    need(bytes<=kMaxBakeBytes,"MOTION_BAKE_CAPACITY: baked frames exceed 2 MiB per document; remove a motion or bake fewer frames");
}
bool applyMotionCommand(Document& d,const V& command,u64 revision,std::string& error){
    const auto* op=command.find("op");if(!op||op->t!=V::Str)return false;const auto& name=op->str;
    if(name!="motion.graph.set"&&name!="motion.bake"&&name!="motion.rename"&&name!="motion.remove")return false;
    try{
        V rows=d.motions.t==V::Arr?d.motions:V::makeArr();
        auto locate=[&](const std::string& key){return std::find_if(rows.arr.begin(),rows.arr.end(),[&](const V& row){const auto* n=row.find("name");return n&&n->str==key;});};
        if(name=="motion.graph.set"){keys(command,{"op","name","graph"},name);const auto key=motionName(command,"name");const auto* graph=command.find("graph");need(graph&&graph->t==V::Obj,"MOTION_INVALID_GRAPH: motion.graph.set requires a graph object");
            (void)checkGraph(d,*graph);auto at=locate(key);
            if(at==rows.arr.end()){need(rows.arr.size()<kMaxMotions,"MOTION_CAPACITY: a document holds at most 32 motions");V row=V::makeObj();row.set("name",V::makeStr(key));row.set("graph",*graph);rows.arr.push_back(row);}else at->set("graph",*graph);}
        else if(name=="motion.bake"){keys(command,{"op","name","fps"},name);const auto key=motionName(command,"name");auto at=locate(key);need(at!=rows.arr.end(),"MOTION_NOT_FOUND: "+key);
            double fps=30;if(const auto* f=command.find("fps")){need(f->t==V::Num&&f->num>=1&&f->num<=kMaxFps&&f->num==std::floor(f->num),"MOTION_INVALID_BAKE: fps must be an integer 1..240");fps=f->num;}
            const V graph=*at->find("graph");auto g=checkGraph(d,graph);need(std::any_of(g.bindings.begin(),g.bindings.end(),[](const MotionJointBinding& b){return b.mate!=0||!b.explodeView.empty();}),"MOTION_INVALID_GRAPH: a bake requires at least one channel bound to a joint mate or an explode_view");
            const auto times=motionFrameTimes(g.compiled.duration,fps);MotionSolvedFrames frames;std::string reason;need(solveMotionFrames(d,g.compiled,g.bindings,times,frames,reason),reason);
            at=locate(key);at->set("bake",encodeBake(g.compiled,g.bindings,frames,fps,revision,motionSourceDigest(d,frames.baseline),motionGraphDigest(graph)));}
        else if(name=="motion.rename"){keys(command,{"op","name","new_name"},name);const auto key=motionName(command,"name"),next=motionName(command,"new_name");auto at=locate(key);need(at!=rows.arr.end(),"MOTION_NOT_FOUND: "+key);need(next==key||locate(next)==rows.arr.end(),"MOTION_NAME_CONFLICT: "+next+" already exists");at->set("name",V::makeStr(next));}
        else{keys(command,{"op","name"},name);const auto key=motionName(command,"name");auto at=locate(key);need(at!=rows.arr.end(),"MOTION_NOT_FOUND: "+key);rows.arr.erase(at);}
        V next=rows.arr.empty()?V{}:std::move(rows);validateMotionStorage(next);d.motions=std::move(next);error.clear();
    }catch(const std::exception& e){error=e.what();}
    return true;
}
#include "motion_clearance_046b.inl"
bool callMotionTool(AgentSession& session,const std::string& tool,const V& args,V& out){
    if(tool!="motion_info")return false;
    keys(args,{"name","graph","time_s","solve","frame","clearance","expected_revision"},"motion_info");Document& d=session.document;
    out=V::makeObj();out.set("revision",V::makeNum((double)session.revision));
    const auto* nameArg=args.find("name");const auto* graphArg=args.find("graph");const auto* timeArg=args.find("time_s");const auto* solveArg=args.find("solve");const auto* frameArg=args.find("frame");
    if(const auto* expected=args.find("expected_revision"))need(expected->t==V::Num&&expected->num==(double)session.revision,"Revision conflict; the current revision is in this result. Re-read motion_info and retry");
    need(!(nameArg&&graphArg),"motion_info accepts name or graph, not both");
    if(const auto* clearance=args.find("clearance")){
        need(nameArg&&nameArg->t==V::Str&&!timeArg&&!solveArg&&!frameArg,"motion_info clearance requires name and excludes graph, time_s, solve and frame");
        out.set("name",*nameArg);out.set("clearance",inspectMotionClearance(d,session.revision,nameArg->str,*clearance));out.set("document_unchanged",V::makeBool(true));out.set("dynamics_checked",V::makeBool(false));out.set("collision_checked",V::makeBool(true));return true;}
    if(timeArg)need(timeArg->t==V::Num&&std::isfinite(timeArg->num)&&timeArg->num>=0&&timeArg->num<=kMaxDuration,"motion_info time_s must be in [0,600]");
    if(solveArg)need(solveArg->t==V::Bool&&(!solveArg->b||timeArg),"motion_info solve must be boolean and requires time_s");
    if(frameArg)need(frameArg->t==V::Num&&frameArg->num>=0&&frameArg->num==std::floor(frameArg->num)&&nameArg,"motion_info frame must be a nonnegative integer with name");
    if(!nameArg&&!graphArg){
        need(!timeArg&&!solveArg&&!frameArg,"motion_info time_s, solve and frame require name or graph");
        AssemblyManipulationProgram program;std::string error;const bool any=d.motions.t==V::Arr&&!d.motions.arr.empty();const bool solved=any&&buildAssemblyManipulation(d,program,error);const std::string digest=solved?motionSourceDigest(d,program.baseline):std::string();
        V rows=V::makeArr();if(any)for(const auto& motion:d.motions.arr){V row=V::makeObj();row.set("name",*motion.find("name"));std::string graphError;
            try{std::vector<MotionJointBinding> bindings;need(solved,"MOTION_JOINT_NOT_DRIVABLE: assembly solve failed: "+error);need(bindMotionJoints(program,d,*motion.find("graph"),bindings,graphError),graphError);std::map<std::string,double> starts;for(const auto& b:bindings)if(b.mate)starts[b.channel]=b.position;
                CompiledMotion compiled;need(compileMotionGraph(*motion.find("graph"),d.buildEnv(),starts,compiled,graphError),graphError);row.set("duration_s",V::makeNum(compiled.duration));row.set("channels",V::makeNum((double)compiled.channels.size()));}
            catch(const std::exception& e){graphError=e.what();}
            if(const auto* bake=motion.find("bake")){row.set("fps",*bake->find("fps"));row.set("frame_count",*bake->find("frame_count"));row.set("baked_instances",V::makeNum((double)bake->find("instances")->arr.size()));if(!row.find("duration_s"))row.set("duration_s",*bake->find("duration_s"));}
            const auto status=motionStatus(motion,solved?&digest:nullptr,graphError);for(const auto& item:status.obj)row.set(item.first,item.second);rows.arr.push_back(row);}
        out.set("motions",rows);out.set("count",V::makeNum((double)rows.arr.size()));out.set("document_unchanged",V::makeBool(true));return true;
    }
    const V* motion=nullptr;V graph;
    if(nameArg){need(nameArg->t==V::Str,"motion_info name must be text");motion=findMotion(d,nameArg->str);need(motion,"MOTION_NOT_FOUND: "+nameArg->str);graph=*motion->find("graph");out.set("name",*nameArg);}else graph=*graphArg;
    std::string graphError;GraphCheck g;bool valid=true;
    try{g=checkGraph(d,graph);}catch(const std::exception& e){if(!motion)throw;graphError=e.what();valid=false;}
    out.set("graph",graph);out.set("valid",V::makeBool(valid));if(valid)out.set("diagnostics",g.diagnostics);else out.set("graph_error",V::makeStr(graphError));
    if(motion){const std::string digest=valid?motionSourceDigest(d,g.program.baseline):std::string();const auto status=motionStatus(*motion,valid?&digest:nullptr,graphError);out.set("status",status);if(const auto* bake=motion->find("bake"))out.set("bake",bakeSummary(*bake));}
    if(timeArg){need(valid,"MOTION_INVALID_GRAPH: "+graphError);const double t=std::min(timeArg->num,g.compiled.duration);std::vector<double> slopes;const auto values=evaluateMotion(g.compiled,t,&slopes);V rows=V::makeArr();
        for(size_t c=0;c<values.size();++c){V row=V::makeObj();row.set("name",V::makeStr(g.compiled.channels[c].name));row.set("value",V::makeNum(values[c]));row.set("velocity_per_s",V::makeNum(slopes[c]));row.set("unit",V::makeStr(!g.bindings[c].unit.empty()?g.bindings[c].unit:"signal"));if(!g.bindings[c].explodeView.empty())row.set("explode_view",V::makeStr(g.bindings[c].explodeView));rows.arr.push_back(row);}
        V evaluation=V::makeObj();evaluation.set("time_s",V::makeNum(t));evaluation.set("channels",rows);
        if(solveArg&&solveArg->b){const double fps=motion&&motion->find("bake")?motion->find("bake")->find("fps")->num:30.;std::vector<double> times;
            if(t<=0)times={0.};else{times=motionFrameTimes(t,fps);}need(times.size()<=kMaxQueryFrames,"MOTION_QUERY_CAPACITY: solving to this time needs more than 600 frames; bake the motion and read frame instead");
            MotionSolvedFrames frames;std::string reason;need(solveMotionFrames(d,g.compiled,g.bindings,times,frames,reason),reason);V instances=V::makeArr();
            for(size_t i=0;i<frames.baseline.size();++i){const auto& world=frames.poses.back()[i];bool moved=false;for(int e=0;e<16;++e)moved|=std::fabs(world.m[e]-frames.baseline[i].world.m[e])>1e-9;if(!moved)continue;V row=V::makeObj(),matrix=V::makeArr();row.set("node",V::makeNum((double)frames.baseline[i].node));row.set("copy",V::makeNum(frames.baseline[i].copy));row.set("name",V::makeStr(frames.baseline[i].name));for(double x:world.m)matrix.arr.push_back(V::makeNum(x));row.set("world_column_major",matrix);instances.arr.push_back(row);}
            V coords=V::makeArr();for(double x:frames.coordinates.back())coords.arr.push_back(V::makeNum(x));evaluation.set("solved_instances",instances);evaluation.set("measured_coordinates",coords);evaluation.set("frames_solved",V::makeNum((double)times.size()));evaluation.set("solver_path",V::makeStr("committed pose, then every frame at the bake rate up to time_s"));}
        out.set("evaluation",evaluation);}
    if(frameArg){BakedMotionFrames frames;std::string reason;need(decodeBakedMotion(*motion,frames,reason),reason);const size_t k=(size_t)frameArg->num;need(k<frames.frameCount,"motion_info frame exceeds frame_count");V instances=V::makeArr();
        for(size_t i=0;i<frames.nodes.size();++i){V row=V::makeObj(),matrix=V::makeArr();row.set("node",V::makeNum((double)frames.nodes[i]));row.set("copy",V::makeNum(frames.copies[i]));row.set("name",V::makeStr(frames.names[i]));for(double x:frames.poses[k*frames.nodes.size()+i].m)matrix.arr.push_back(V::makeNum(x));row.set("world_column_major",matrix);instances.arr.push_back(row);}
        V coords=V::makeArr();for(size_t c=0;c<frames.channels.size();++c)coords.arr.push_back(V::makeNum(frames.coordinates[k*frames.channels.size()+c]));V frame=V::makeObj();frame.set("index",V::makeNum((double)k));frame.set("time_s",V::makeNum(std::min(frames.duration,(double)k/frames.fps)));frame.set("instances",instances);frame.set("coordinates",coords);frame.set("precision",V::makeStr("float32 display poses"));out.set("frame",frame);}
    out.set("document_unchanged",V::makeBool(true));out.set("dynamics_checked",V::makeBool(false));out.set("collision_checked",V::makeBool(false));return true;
}
void appendMotionTools(V& tools){
    V tool=V::makeObj(),schema,annotations=V::makeObj();
    arkjson::parse(R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":128},"graph":{"type":"object","description":"Uncommitted vector motion graph to validate; same shape as motion.graph.set graph."},"time_s":{"type":"number","minimum":0,"maximum":600},"solve":{"type":"boolean","description":"With time_s, drive the joints through the assembly solver from the committed pose to time_s and return solved instance matrices. Read-only."},"frame":{"type":"integer","minimum":0},"expected_revision":{"type":"integer","description":"Optional observed revision; a mismatch rejects with REVISION_CONFLICT."},"clearance":{"type":"object","description":"With name only: read-only exact clearance over a fresh bake. Rejects a stale bake (bake again first). Measures exact B-rep distance at every baked frame and certifies each frame interval with a conservative displacement bound, bisecting undecided intervals through the joint solver. continuous_certified:true means every interval is proven contact free under ideal rigid kinematics.","properties":{"pairs":{"type":"array","minItems":1,"maxItems":256,"items":{"type":"object","properties":{"a":{"type":["integer","string"]},"b":{"type":["integer","string"]},"copy_a":{"type":"integer","minimum":0,"maximum":31},"copy_b":{"type":"integer","minimum":0,"maximum":31}},"required":["a","b"],"additionalProperties":false}},"scope":{"type":"string","enum":["all","static"],"description":"Without pairs: each moving occurrence against every other visible solid (all, default) or only non-moving ones (static); rigidly co-moving and directly mated pairs are skipped and counted."},"threshold_mm":{"type":"number","minimum":0,"maximum":1000},"max_subdivisions":{"type":"integer","minimum":0,"maximum":10},"from_frame":{"type":"integer","minimum":0},"to_frame":{"type":"integer","minimum":1}},"additionalProperties":false}},"additionalProperties":false})",schema);
    tool.set("name",V::makeStr("motion_info"));tool.set("description",V::makeStr("Read-only Skeleton Motion inspection. No arguments lists baked motions with duration, fps and stale status. name returns its vector graph, joint bindings, C0/C1 continuity, sampled ranges and bake summary; graph validates an uncommitted graph. time_s evaluates channel values and velocities; solve:true also returns solved instance matrices at that time. frame decodes one baked frame. name with clearance{pairs|scope,threshold_mm,max_subdivisions} checks exact clearance over a fresh bake and certifies between frames (continuous_certified); a stale bake rejects. Never edits the document; author with doc_apply motion.graph.set and motion.bake (agent_onboard motion)."));
    annotations.set("readOnlyHint",V::makeBool(true));tool.set("inputSchema",schema);tool.set("annotations",annotations);tools.arr.push_back(tool);
}
}
