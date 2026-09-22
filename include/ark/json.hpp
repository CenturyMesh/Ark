// ARK-Point :: arkjson.hpp — minimal dependency-free JSON for the MCP bridge.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>

namespace arkjson {

struct Value {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b=false;
    double num=0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string,Value>> obj;

    static Value makeObj(){ Value v; v.t=Obj; return v; }
    static Value makeArr(){ Value v; v.t=Arr; return v; }
    static Value makeStr(std::string s){ Value v; v.t=Str; v.str=std::move(s); return v; }
    static Value makeNum(double d){ Value v; v.t=Num; v.num=d; return v; }
    static Value makeBool(bool bb){ Value v; v.t=Bool; v.b=bb; return v; }

    void set(const std::string& k, Value v){
        for(auto&kv:obj) if(kv.first==k){ kv.second=std::move(v); return; }
        obj.emplace_back(k,std::move(v));
    }
    const Value* find(const char* k) const {
        if(t!=Obj) return nullptr;
        for(auto&kv:obj) if(kv.first==k) return &kv.second;
        return nullptr;
    }
    double asNum(double dflt=0) const { return t==Num?num:dflt; }
    std::string asStr(const char* dflt="") const { return t==Str?str:(dflt?dflt:""); }
    bool isNull() const { return t==Null; }

    std::string dump() const {
        std::string o;
        write(o);
        return o;
    }
    void write(std::string& o) const {
        switch(t){
            case Null: o+="null"; break;
            case Bool: o+=b?"true":"false"; break;
            case Num: {
                if(!std::isfinite(num)){ o+="null"; break; }
                char buf[32];
                if(num==std::floor(num) && std::fabs(num)<1e15)
                    std::snprintf(buf,sizeof buf,"%lld",(long long)num);
                else
                    std::snprintf(buf,sizeof buf,"%.17g",num);
                o+=buf;
                break;
            }
            case Str: quote(o,str); break;
            case Arr: {
                o+='[';
                for(size_t i=0;i<arr.size();++i){ if(i)o+=','; arr[i].write(o); }
                o+=']';
                break;
            }
            case Obj: {
                o+='{';
                for(size_t i=0;i<obj.size();++i){
                    if(i)o+=',';
                    quote(o,obj[i].first);
                    o+=':';
                    obj[i].second.write(o);
                }
                o+='}';
                break;
            }
        }
    }
    static void quote(std::string& o,const std::string& s){
        o+='"';
        for(unsigned char c:s){
            switch(c){
                case '"': o+="\\\""; break;
                case '\\': o+="\\\\";
                    break;
                case '\n': o+="\\n"; break;
                case '\r': o+="\\r"; break;
                case '\t': o+="\\t"; break;
                default:
                    if(c<0x20){ char buf[8]; std::snprintf(buf,sizeof buf,"\\u%04x",c); o+=buf; }
                    else o+=(char)c;
            }
        }
        o+='"';
    }
};

struct Parser {
    const std::string& s; size_t i=0; unsigned depth=0;
    explicit Parser(const std::string& src):s(src){}

    void ws(){ while(i<s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n')) ++i; }
    bool fail(Value& out){ out=Value{}; return false; }

    bool parse(Value& out){
        ws();
        if(!parseVal(out)) return false;
        ws();
        return i>=s.size();
    }
    bool parseVal(Value& v){
        if(depth>=64)return fail(v);
        struct DepthGuard{unsigned& depth;DepthGuard(unsigned& d):depth(d){++depth;}~DepthGuard(){--depth;}} guard(depth);
        ws();
        if(i>=s.size()) return fail(v);
        char c=s[i];
        if(c=='{') return parseObj(v);
        if(c=='[') return parseArr(v);
        if(c=='"'){ v.t=Value::Str; return parseStr(v.str); }
        if(c=='t'){ if(s.compare(i,4,"true")==0){i+=4;v=Value::makeBool(true);return true;} return false; }
        if(c=='f'){ if(s.compare(i,5,"false")==0){i+=5;v=Value::makeBool(false);return true;} return false; }
        if(c=='n'){ if(s.compare(i,4,"null")==0){i+=4;v=Value{};return true;} return false; }
        // JSON number grammar: no NaN/Infinity, leading plus/zero, or hex.
        size_t start=i;
        if(s[i]=='-')++i;
        if(i>=s.size())return fail(v);
        if(s[i]=='0')++i;
        else {if(s[i]<'1'||s[i]>'9')return fail(v);while(i<s.size()&&std::isdigit((unsigned char)s[i]))++i;}
        if(i<s.size()&&s[i]=='.'){++i;size_t n=i;while(i<s.size()&&std::isdigit((unsigned char)s[i]))++i;if(i==n)return fail(v);}
        if(i<s.size()&&(s[i]=='e'||s[i]=='E')){++i;if(i<s.size()&&(s[i]=='+'||s[i]=='-'))++i;size_t n=i;while(i<s.size()&&std::isdigit((unsigned char)s[i]))++i;if(i==n)return fail(v);}
        double d=std::strtod(s.substr(start,i-start).c_str(),nullptr);
        if(!std::isfinite(d))return fail(v);
        v=Value::makeNum(d);return true;
    }

    bool parseObj(Value& v){
        ++i; v=Value::makeObj();
        ws();
        if(i<s.size()&&s[i]=='}'){++i;return true;}
        for(;;){
            ws();
            std::string key;
            if(i>=s.size()||s[i]!='"') return fail(v);
            if(!parseStr(key)) return false;
            ws();
            if(i>=s.size()||s[i]!=':') return fail(v);
            ++i;
            Value child;
            if(!parseVal(child)) return false;
            if(v.find(key.c_str()))return fail(v);
            v.obj.emplace_back(std::move(key),std::move(child));
            ws();
            if(i<s.size()&&s[i]==','){++i;continue;}
            if(i<s.size()&&s[i]=='}'){++i;return true;}
            return fail(v);
        }
    }
    bool parseArr(Value& v){
        ++i; v=Value::makeArr();
        ws();
        if(i<s.size()&&s[i]==']'){++i;return true;}
        for(;;){
            Value c;
            if(!parseVal(c)) return false;
            v.arr.push_back(std::move(c));
            ws();
            if(i<s.size()&&s[i]==','){++i;continue;}
            if(i<s.size()&&s[i]==']'){++i;return true;}
            return fail(v);
        }
    }
    bool parseStr(std::string& out){
        ++i; // skip "
        while(i<s.size()){
            char c=s[i++];
            if(c=='"') return true;
            if(c=='\\'){
                if(i>=s.size()) return false;
                char e=s[i++];
                switch(e){
                    case '"': out+='"'; break;
                    case '\\':out+='\\';break;
                    case '/': out+='/'; break;
                    case 'b': out+='\b';break;
                    case 'f': out+='\f';break;
                    case 'n': out+='\n';break;
                    case 'r': out+='\r';break;
                    case 't': out+='\t';break;
                    case 'u': {
                        if(i+4>s.size()) return false;
                        for(size_t k=0;k<4;++k)if(!std::isxdigit((unsigned char)s[i+k]))return false;
                        unsigned cp=(unsigned)strtoul(s.substr(i,4).c_str(),nullptr,16);
                        i+=4;
                        // surrogate pair
                        if(cp>=0xD800&&cp<=0xDBFF&&i+6<=s.size()&&s[i]=='\\'&&s[i+1]=='u'){
                            for(size_t k=0;k<4;++k)if(!std::isxdigit((unsigned char)s[i+2+k]))return false;
                            unsigned lo=(unsigned)strtoul(s.substr(i+2,4).c_str(),nullptr,16);
                            if(lo<0xDC00||lo>0xDFFF)return false;
                            cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00);
                            i+=6;
                        }
                        if(cp>=0xD800&&cp<=0xDFFF)return false;
                        // UTF-8 encode
                        if(cp<0x80) out+=(char)cp;
                        else if(cp<0x800){ out+=(char)(0xC0|cp>>6); out+=(char)(0x80|(cp&63)); }
                        else if(cp<0x10000){ out+=(char)(0xE0|cp>>12); out+=(char)(0x80|((cp>>6)&63)); out+=(char)(0x80|(cp&63)); }
                        else { out+=(char)(0xF0|cp>>18); out+=(char)(0x80|((cp>>12)&63)); out+=(char)(0x80|((cp>>6)&63)); out+=(char)(0x80|(cp&63)); }
                        break;
                    }
                    default: return false;
                }
            } else {if((unsigned char)c<0x20)return false;out+=c;}
        }
        return false;
    }
};

inline bool parse(const std::string& src, Value& out){ return Parser(src).parse(out); }

} // namespace arkjson
