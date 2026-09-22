// ARK-Point :: serialize.cpp — document persistence over arkjson.
#include "ark/serialize.hpp"
#include <cstdio>
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

namespace {

Value entToJson(const SketchEnt& e){
    Value v=Value::makeObj();
    const char* k = e.kind==SketchEnt::Pnt?"point":e.kind==SketchEnt::Line?"line":"circle";
    v.set("kind",Value::makeStr(k));
    Value d=Value::makeArr();
    for(f64 x:e.dof) d.arr.push_back(Value::makeNum(x));
    v.set("dof",std::move(d));
    return v;
}

bool entFromJson(const Value& v, SketchEnt& e, std::string* err){
    std::string k=v.find("kind")?v.find("kind")->asStr("point"):"point";
    if(k=="point") e.kind=SketchEnt::Pnt;
    else if(k=="line") e.kind=SketchEnt::Line;
    else if(k=="circle") e.kind=SketchEnt::Circle;
    else { if(err)*err="unknown sketch entity kind: "+k; return false; }
    if(auto d=v.find("dof")){
        if(d->t!=Value::Arr){if(err)*err="dof must be array";return false;}
        for(auto&x:d->arr){if(x.t!=Value::Num||!std::isfinite(x.num)){if(err)*err="dof must be finite numbers";return false;} e.dof.push_back(x.num);}
    }
    if(e.dof.empty()){                       // sensible defaults for quick sketches
        if(e.kind==SketchEnt::Pnt) e.dof={0,0};
        else if(e.kind==SketchEnt::Line) e.dof={0,0,10,0};
        else e.dof={0,0,5};
    }
    const size_t expected=e.kind==SketchEnt::Pnt?2:e.kind==SketchEnt::Line?4:3;
    if(e.dof.size()!=expected){if(err)*err="wrong entity degrees of freedom";return false;}
    return true;
}

bool validSketch(const Sketch& sk,std::string* err){
    auto fail=[&](const char* e){if(err)*err=e;return false;};
    if(sk.ents.size()>128||sk.cons.size()>256)return fail("sketch capacity exceeded");
    for(auto& c:sk.cons){
        auto valid=[&](int e,int slot){return e>=0&&(size_t)e<sk.ents.size()&&slot>=0&&slot<=(sk.ents[e].kind==SketchEnt::Line?1:0);};
        if(!valid(c.a,c.pa))return fail("invalid constraint entity or point slot");
        bool pair=c.type==SketchCon::DistPP||c.type==SketchCon::Coincident||c.type==SketchCon::Parallel||c.type==SketchCon::Perpendicular||c.type==SketchCon::AngleLL;
        if(pair&&!valid(c.b,c.pb))return fail("invalid second constraint entity");
        bool lines=c.type==SketchCon::Parallel||c.type==SketchCon::Perpendicular||c.type==SketchCon::AngleLL;
        if((lines||c.type==SketchCon::Horizontal||c.type==SketchCon::Vertical)&&sk.ents[c.a].kind!=SketchEnt::Line)return fail("constraint requires line");
        if(lines&&sk.ents[c.b].kind!=SketchEnt::Line)return fail("constraint requires two lines");
        if(c.type==SketchCon::Radius&&sk.ents[c.a].kind!=SketchEnt::Circle)return fail("radius requires circle");
    }
    return true;
}

Value conToJson(const SketchCon& c){
    Value v=Value::makeObj();
    v.set("type",Value::makeStr(constraintTypeName(c.type)));
    v.set("a",Value::makeNum(c.a));
    v.set("b",Value::makeNum(c.b));
    v.set("pa",Value::makeNum(c.pa));
    v.set("pb",Value::makeNum(c.pb));
    v.set("val",Value::makeNum(c.val));
    v.set("val2",Value::makeNum(c.val2));
    return v;
}

bool conFromJson(const Value& v, SketchCon& c, std::string* err){
    i32 t=0;
    std::string tn=v.find("type")?v.find("type")->asStr():"";
    if(!constraintTypeFromName(tn,t)){ if(err)*err="unknown constraint type: "+tn; return false; }
    c.type=(SketchCon::Type)t;
    c.a =(i32)(v.find("a") ?v.find("a") ->asNum(-1): -1);
    c.b =(i32)(v.find("b") ?v.find("b") ->asNum(-1): -1);
    c.pa=(i32)(v.find("pa")?v.find("pa")->asNum(0) : 0);
    c.pb=(i32)(v.find("pb")?v.find("pb")->asNum(0) : 0);
    c.val =v.find("val") ?v.find("val") ->asNum():0;
    c.val2=v.find("val2")?v.find("val2")->asNum():0;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
std::string documentToJson(const Document& doc){
    Value r=Value::makeObj();
    r.set("format",Value::makeStr("ark"));
    bool sourceTube=false;bool notched=false;bool extended=!doc.assembly.empty()||doc.nextAssemblyId_>1;for(auto& f:doc.features){extended|=f.type==FeatureType::Tube;notched|=f.type==FeatureType::NotchedTube;sourceTube|=f.type==FeatureType::SourceTube;}
    r.set("version",Value::makeNum(sourceTube?4:notched?3:extended?2:1));
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

    Value sks=Value::makeArr();
    for(auto&ns:doc.sketches){
        Value v=Value::makeObj();
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
        v.set("op",Value::makeNum(f.opInt));
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
        fs.arr.push_back(std::move(v));
    }
    r.set("features",std::move(fs));
    if(extended){
        r.set("next_assembly_id",Value::makeNum((double)doc.nextAssemblyId_));Value nodes=Value::makeArr();
        for(auto& n:doc.assembly){Value j=Value::makeObj(),p=Value::makeObj();j.set("id",Value::makeNum((double)n.id));j.set("parent",Value::makeNum((double)n.parent));j.set("feature",Value::makeNum((double)n.feature));j.set("name",Value::makeStr(n.name));j.set("visible",Value::makeBool(n.visible));
            std::vector<std::string> keys;for(auto& kv:n.p)keys.push_back(kv.first);std::sort(keys.begin(),keys.end());for(auto& k:keys)p.set(k,Value::makeStr(n.p.at(k)));j.set("p",p);nodes.arr.push_back(j);}
        r.set("assembly",nodes);
    }
    return r.dump();
}

bool documentFromJson(const std::string& json, Document& doc, std::string* err){
    auto fail=[&](const std::string& m){ if(err)*err=m; return false; };
    Value v;
    if(!arkjson::parse(json,v)) return fail("malformed JSON");
    if(v.t!=Value::Obj) return fail("root must be object");
    if(auto ver=v.find("version"); ver && (ver->t!=Value::Num || (ver->num!=1.0&&ver->num!=2.0&&ver->num!=3.0&&ver->num!=4.0)))
        return fail("unsupported document version");

    if(auto format=v.find("format");format && format->asStr()!="ark")return fail("unsupported format");
    if(auto units=v.find("units");units && units->asStr()!="mm")return fail("unsupported units (expected mm)");
    if(!v.find("features"))return fail("missing features array");
    for(auto key:{"params","sketches","features"})if(auto a=v.find(key);a && a->t!=Value::Arr)return fail(std::string(key)+" must be an array");
    Document nd;   // parse into scratch; commit only on full success
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

    if(auto sks=v.find("sketches"))
        for(auto&s:sks->arr){
            NamedSketch ns;
            ns.name=s.find("name")?s.find("name")->asStr():"";
            if(ns.name.empty() || !sketchNames.insert(ns.name).second) return fail("empty or duplicate sketch name");
            if(auto es=s.find("entities"))
                for(auto&e:es->arr){
                    SketchEnt se;
                    if(!entFromJson(e,se,err)) return false;
                    ns.sk.ents.push_back(std::move(se));
                }
            if(auto cs=s.find("constraints"))
                for(auto&c:cs->arr){
                    SketchCon sc;
                    if(!conFromJson(c,sc,err)) return false;
                    ns.sk.cons.push_back(sc);
                }
            if(!validSketch(ns.sk,err))return false;
            nd.sketches.push_back(std::move(ns));
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
            nf.opInt=(int)(fv.find("op")?fv.find("op")->asNum():0);
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
            if(nf.type==FeatureType::SourceTube){auto version=v.find("version"),data=fv.find("source");if(!version||version->asNum()!=4||!data||data->t!=Value::Obj)return fail("source_tube requires version 4 package");nf.sourceData=*data;}
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
            maximum=std::max(maximum,n.id);nd.assembly.push_back(n);
        }
        nd.nextAssemblyId_=maximum+1;
        if(v.find("next_assembly_id")){u64 counter;if(!integer(v,"next_assembly_id",counter)||counter<=maximum)return fail("invalid next_assembly_id");nd.nextAssemblyId_=counter;}
    }
    if(nd.params.size()>256||nd.features.size()>1024||nd.sketches.size()>64)return fail("document capacity exceeded");
    nd.refreshParams();
    if(!nd.parameterError.empty())return fail(nd.parameterError);
    std::vector<AssemblyPlacement> placements;std::string assemblyError;if(!resolveAssembly(nd,placements,assemblyError))return fail(assemblyError);
    doc=std::move(nd);
    return true;
}

// ---------------------------------------------------------------------------
std::string sketchToJson(const Sketch& sk){
    Value v=Value::makeObj();
    Value es=Value::makeArr(); for(auto&e:sk.ents) es.arr.push_back(entToJson(e));
    Value cs=Value::makeArr(); for(auto&c:sk.cons) cs.arr.push_back(conToJson(c));
    v.set("entities",std::move(es));
    v.set("constraints",std::move(cs));
    return v.dump();
}

bool sketchFromJson(const std::string& json, Sketch& sk, std::string* err){
    Value v;
    if(!arkjson::parse(json,v)){ if(err)*err="malformed JSON"; return false; }
    Sketch out;
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
    if(!validSketch(out,err))return false;
    sk=std::move(out);
    return true;
}

bool saveDocument(const Document& doc, const char* path){
    if(!path||!*path) return false;
    auto destination=std::filesystem::path(reinterpret_cast<const char8_t*>(path)),temporary=destination;
    temporary+=".tmp";
    std::ofstream f(temporary,std::ios::binary|std::ios::trunc);
    if(!f) return false;
    std::string j=documentToJson(doc);
    f.write(j.data(),(std::streamsize)j.size());f.flush();
    if(!f)return false;
    f.close();if(!f)return false;
#ifdef _WIN32
    bool committed=MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code ec;std::filesystem::rename(temporary,destination,ec);bool committed=!ec;
#endif
    if(!committed){std::error_code cleanup;std::filesystem::remove(temporary,cleanup);}
    return committed;
}

bool loadDocument(const char* path, Document& doc, std::string* err){
    if(!path||!*path){ if(err)*err="empty path"; return false; }
    std::ifstream f(std::filesystem::path(reinterpret_cast<const char8_t*>(path)),std::ios::binary);
    if(!f){ if(err)*err=std::string("cannot open ")+path; return false; }
    f.seekg(0,std::ios::end);auto length=f.tellg();
    if(length<0||length>4*1024*1024){if(err)*err="document exceeds 4 MiB";return false;}f.seekg(0);
    std::string j((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    return documentFromJson(j,doc,err);
}

} // namespace ark
