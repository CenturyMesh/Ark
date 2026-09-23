// ARK-Point :: serialize.cpp — document persistence over arkjson.
#include "ark/serialize.hpp"
#include "ark/sketch.hpp"
#include "ark/assembly_io.hpp"
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
    const char* k = e.kind==SketchEnt::Pnt?"point":e.kind==SketchEnt::Line?"line":e.kind==SketchEnt::Circle?"circle":"spline";
    v.set("kind",Value::makeStr(k));
    Value d=Value::makeArr();
    for(size_t i=0;i<e.dof.size();++i)d.arr.push_back(i<e.expressions.size()&&!e.expressions[i].empty()?Value::makeStr(e.expressions[i]):Value::makeNum(e.dof[i]));
    v.set("dof",std::move(d));
    if(!e.name.empty())v.set("name",Value::makeStr(e.name));
    if(e.construction)v.set("construction",Value::makeBool(true));
    if(e.kind==SketchEnt::Spline){v.set("closed",Value::makeBool(e.closed));v.set("tension",Value::makeNum(e.tension));}
    return v;
}

bool entFromJson(const Value& v, SketchEnt& e, std::string* err){
    auto fail=[&](const std::string& m){if(err)*err=m;return false;};
    if(v.t!=Value::Obj)return fail("Sketch entity must be object");
    std::string k=v.find("kind")?v.find("kind")->asStr("point"):"point";
    if(k=="point") e.kind=SketchEnt::Pnt;
    else if(k=="line") e.kind=SketchEnt::Line;
    else if(k=="circle") e.kind=SketchEnt::Circle;
    else if(k=="spline") e.kind=SketchEnt::Spline;
    else { if(err)*err="unknown sketch entity kind: "+k; return false; }
    if(auto d=v.find("dof")){
        if(d->t!=Value::Arr){if(err)*err="dof must be array";return false;}
        for(auto&x:d->arr){
            if(x.t==Value::Str&&!x.str.empty()&&x.str.size()<=2048){e.dof.push_back(0);e.expressions.push_back(x.str);}
            else if(x.t==Value::Num&&std::isfinite(x.num)&&std::abs(x.num)<=1e7){e.dof.push_back(x.num);e.expressions.emplace_back();}
            else return fail("dof must be finite numbers or nonempty expressions");
        }
    }
    if(e.dof.empty()){                       // sensible defaults for quick sketches
        if(e.kind==SketchEnt::Pnt) e.dof={0,0};
        else if(e.kind==SketchEnt::Line) e.dof={0,0,10,0};
        else if(e.kind==SketchEnt::Circle)e.dof={0,0,5};
        else return fail("Spline requires explicit anchor coordinates");
    }
    const size_t expected=e.kind==SketchEnt::Pnt?2:e.kind==SketchEnt::Line?4:3;
    if(e.kind!=SketchEnt::Spline&&e.dof.size()!=expected)return fail("wrong entity degrees of freedom");
    if(auto n=v.find("name")){if(n->t!=Value::Str||n->str.empty()||n->str.size()>128)return fail("entity name must be 1..128 characters");e.name=n->str;}
    for(auto pair:{std::pair{"construction",&e.construction},std::pair{"closed",&e.closed}})if(auto b=v.find(pair.first)){if(b->t!=Value::Bool)return fail("entity flags must be boolean");*pair.second=b->b;}
    if(auto t=v.find("tension")){if(t->t!=Value::Num||!std::isfinite(t->num)||t->num<0||t->num>1)return fail("Spline tension must be 0..1");e.tension=t->num;}
    if(e.kind==SketchEnt::Spline&&(e.dof.size()<(e.closed?6u:4u)||e.dof.size()>128||e.dof.size()%2))return fail("Spline requires 2..64 XY anchors; closed requires at least 3");
    return true;
}

bool validSketch(const Sketch& sk,std::string* err){
    auto fail=[&](const char* e){if(err)*err=e;return false;};
    if(sk.ents.size()>128||sk.cons.size()>256)return fail("sketch capacity exceeded");
    size_t variables=0;std::set<std::string> names;
    for(auto& e:sk.ents){if(!e.name.empty()&&!names.insert(e.name).second)return fail("duplicate sketch entity name");variables+=e.dof.size();}
    if(variables>512)return fail("Sketch exceeds 512 scalar coordinates");
    std::set<std::string> constraintIds;
    for(auto& c:sk.cons){
        if(!c.id.empty()&&!constraintIds.insert(c.id).second)return fail("Duplicate sketch constraint id");
        auto valid=[&](int e,int slot){return e>=0&&(size_t)e<sk.ents.size()&&slot>=0&&slot<(sk.ents[e].kind==SketchEnt::Spline?(int)sk.ents[e].dof.size()/2:sk.ents[e].kind==SketchEnt::Line?2:1);};
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
    if(!c.id.empty())v.set("id",Value::makeStr(c.id));
    v.set("type",Value::makeStr(constraintTypeName(c.type)));
    v.set("a",Value::makeNum(c.a));
    v.set("b",Value::makeNum(c.b));
    v.set("pa",Value::makeNum(c.pa));
    v.set("pb",Value::makeNum(c.pb));
    v.set("val",c.expression.empty()?Value::makeNum(c.val):Value::makeStr(c.expression));
    v.set("val2",c.expression2.empty()?Value::makeNum(c.val2):Value::makeStr(c.expression2));
    return v;
}

bool conFromJson(const Value& v, SketchCon& c, std::string* err){
    auto fail=[&](const char* m){if(err)*err=m;return false;};
    if(v.t!=Value::Obj)return fail("Constraint must be object");
    if(auto key=v.find("id")){if(key->t!=Value::Str||key->str.empty()||key->str.size()>128||key->str.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos)return fail("Constraint id needs 1..128 letters, numbers, dot, underscore or hyphen");c.id=key->str;}
    i32 t=0;
    std::string tn=v.find("type")?v.find("type")->asStr():"";
    if(!constraintTypeFromName(tn,t)){ if(err)*err="unknown constraint type: "+tn; return false; }
    c.type=(SketchCon::Type)t;
    for(auto pair:{std::pair{"a",&c.a},std::pair{"b",&c.b},std::pair{"pa",&c.pa},std::pair{"pb",&c.pb}})if(auto p=v.find(pair.first)){
        if(p->t!=Value::Num||!std::isfinite(p->num)||p->num< -1||p->num>512||p->num!=std::floor(p->num))return fail("Constraint indexes must be integers");
        *pair.second=(int)p->num;
    }
    for(auto key:{"val","val2"})if(auto p=v.find(key)){
        auto& expr=std::string(key)=="val"?c.expression:c.expression2;auto& number=std::string(key)=="val"?c.val:c.val2;
        if(p->t==Value::Str&&!p->str.empty()&&p->str.size()<=2048)expr=p->str;
        else if(p->t==Value::Num&&std::isfinite(p->num))number=p->num;
        else return fail("Dimension must be finite number or expression");
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
std::string documentToJson(const Document& doc){
    Value r=Value::makeObj();
    r.set("format",Value::makeStr("ark"));
    bool sourceTube=false;bool notched=false;bool nativeVersion=false;bool extended=!doc.assembly.empty()||doc.nextAssemblyId_>1;for(auto& f:doc.features){extended|=f.type==FeatureType::Tube;notched|=f.type==FeatureType::NotchedTube;sourceTube|=f.type==FeatureType::SourceTube;nativeVersion|=f.type==FeatureType::Loft||f.type==FeatureType::SurfaceFill||f.type==FeatureType::BSplineSurface||f.type==FeatureType::Thicken||f.type==FeatureType::SheetMetal||f.type==FeatureType::SourceSheet||f.type==FeatureType::NativeSolid;}
    bool sketchVersion=!doc.planes.empty()||!doc.sketches.empty();for(auto& f:doc.features)sketchVersion|=f.type==FeatureType::SketchCut;
    bool latest=!doc.mates.empty()||doc.nextMateId_>1;for(auto& p:doc.planes)latest|=p.support.t==Value::Obj||p.kind!="plane";for(auto& f:doc.features)latest|=f.type==FeatureType::NativeSolid||(f.type==FeatureType::SheetMetal&&f.nativeSpec.find("base"));for(auto& n:doc.assembly)latest|=n.fixed;for(auto& sk:doc.sketches)for(auto& c:sk.sk.cons)latest|=!c.id.empty();
    r.set("version",Value::makeNum(latest?7:nativeVersion?6:sketchVersion?5:sourceTube?4:notched?3:extended?2:1));
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
    if(!doc.planes.empty()){
        Value planes=Value::makeArr();for(auto& plane:doc.planes){Value v=Value::makeObj(),p=Value::makeObj();v.set("name",Value::makeStr(plane.name));v.set("parent",Value::makeStr(plane.parent));
            std::vector<std::string> keys;for(auto& kv:plane.p)keys.push_back(kv.first);std::sort(keys.begin(),keys.end());for(auto& key:keys)p.set(key,Value::makeStr(plane.p.at(key)));v.set("p",p);v.set("kind",Value::makeStr(plane.kind));if(plane.support.t==Value::Obj)v.set("support",plane.support);planes.arr.push_back(v);
        }r.set("planes",planes);
    }

    Value sks=Value::makeArr();
    for(auto&ns:doc.sketches){
        Value v;arkjson::parse(sketchToJson(ns.sk),v);
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
        if(f.type==FeatureType::SourceSheet)v.set("source",f.sourceData);
        if(f.type==FeatureType::Loft||f.type==FeatureType::SurfaceFill||f.type==FeatureType::BSplineSurface||f.type==FeatureType::Thicken||f.type==FeatureType::SheetMetal||f.type==FeatureType::NativeSolid)
            v.set("native",f.nativeSpec.t==Value::Obj?f.nativeSpec:Value::makeObj());
        fs.arr.push_back(std::move(v));
    }
    r.set("features",std::move(fs));
    if(extended){
        r.set("next_assembly_id",Value::makeNum((double)doc.nextAssemblyId_));Value nodes=Value::makeArr();
        for(auto& n:doc.assembly){Value j=Value::makeObj(),p=Value::makeObj();j.set("id",Value::makeNum((double)n.id));j.set("parent",Value::makeNum((double)n.parent));j.set("feature",Value::makeNum((double)n.feature));j.set("name",Value::makeStr(n.name));j.set("visible",Value::makeBool(n.visible));j.set("fixed",Value::makeBool(n.fixed));
            std::vector<std::string> keys;for(auto& kv:n.p)keys.push_back(kv.first);std::sort(keys.begin(),keys.end());for(auto& k:keys)p.set(k,Value::makeStr(n.p.at(k)));j.set("p",p);nodes.arr.push_back(j);}
        r.set("assembly",nodes);
    }
    if(!doc.mates.empty()||doc.nextMateId_>1){Value mates=Value::makeArr();for(auto& m:doc.mates)mates.arr.push_back(mateToJson(m));r.set("mates",mates);r.set("next_mate_id",Value::makeNum((double)doc.nextMateId_));}
    return r.dump();
}

bool documentFromJson(const std::string& json, Document& doc, std::string* err){
    auto fail=[&](const std::string& m){ if(err)*err=m; return false; };
    Value v;
    if(!arkjson::parse(json,v)) return fail("malformed JSON");
    if(v.t!=Value::Obj) return fail("root must be object");
    if(auto ver=v.find("version"); ver && (ver->t!=Value::Num || (ver->num!=1.0&&ver->num!=2.0&&ver->num!=3.0&&ver->num!=4.0&&ver->num!=5.0&&ver->num!=6.0&&ver->num!=7.0)))
        return fail("unsupported document version");

    if(auto format=v.find("format");format && format->asStr()!="ark")return fail("unsupported format");
    if(auto units=v.find("units");units && units->asStr()!="mm")return fail("unsupported units (expected mm)");
    if(!v.find("features"))return fail("missing features array");
    for(auto key:{"params","sketches","features","planes"})if(auto a=v.find(key);a && a->t!=Value::Arr)return fail(std::string(key)+" must be an array");
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
            if(!sketchFromJson(s.dump(),ns.sk,err))return false;
            nd.sketches.push_back(std::move(ns));
        }

    if(auto planes=v.find("planes")){
        if(planes->arr.size()>128)return fail("Plane capacity exceeded");
        std::set<std::string> names{"XY","XZ","YZ"};
        for(auto& j:planes->arr){DatumPlane plane;
            auto name=j.find("name");if(!name||name->t!=Value::Str||name->str.empty()||name->str.size()>128||!names.insert(name->str).second)return fail("Invalid or duplicate plane name");plane.name=name->str;
            if(auto parent=j.find("parent")){if(parent->t!=Value::Str||parent->str.empty())return fail("Plane parent must be a name");plane.parent=parent->str;}
            if(auto p=j.find("p")){if(p->t!=Value::Obj)return fail("Plane p must be object");for(auto& kv:p->obj){if(kv.second.t!=Value::Str)return fail("Plane values must be expressions");plane.p[kv.first]=kv.second.str;}}
            if(auto kind=j.find("kind")){if(kind->t!=Value::Str||(kind->str!="plane"&&kind->str!="axis"&&kind->str!="frame"))return fail("Invalid datum kind");plane.kind=kind->str;}
            if(auto support=j.find("support")){if(support->t!=Value::Obj)return fail("Datum support must be object");plane.support=*support;}
            nd.planes.push_back(std::move(plane));
        }
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
            if(nf.type==FeatureType::SourceTube){auto version=v.find("version"),data=fv.find("source");if(!version||version->asNum()<4||!data||data->t!=Value::Obj)return fail("source_tube requires version 4 package");nf.sourceData=*data;}
            if(nf.type==FeatureType::SourceSheet){auto version=v.find("version"),data=fv.find("source");if(!version||version->asNum()<6||!data||data->t!=Value::Obj)return fail("source_sheet requires version 6 package");nf.sourceData=*data;}
            if(nf.type==FeatureType::Loft||nf.type==FeatureType::SurfaceFill||nf.type==FeatureType::BSplineSurface||nf.type==FeatureType::Thicken||nf.type==FeatureType::SheetMetal||nf.type==FeatureType::NativeSolid){auto version=v.find("version"),spec=fv.find("native");if(!version||version->asNum()<6||!spec||spec->t!=Value::Obj)return fail("native feature requires version 6 native object");nf.nativeSpec=*spec;}
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
            if(auto fixed=j.find("fixed")){if(fixed->t!=Value::Bool)return fail("fixed must be boolean");n.fixed=fixed->b;}
            maximum=std::max(maximum,n.id);nd.assembly.push_back(n);
        }
        nd.nextAssemblyId_=maximum+1;
        if(v.find("next_assembly_id")){u64 counter;if(!integer(v,"next_assembly_id",counter)||counter<=maximum)return fail("invalid next_assembly_id");nd.nextAssemblyId_=counter;}
    }
    if(auto list=v.find("mates")){
        if(list->t!=Value::Arr||list->arr.size()>256)return fail("Invalid mates array");
        std::set<u64> seen;try{for(auto& j:list->arr){auto key=j.find("id");if(!key||key->t!=Value::Num||key->num<1||key->num>9007199254740990.||key->num!=std::floor(key->num))return fail("Invalid mate id");AssemblyMate m;m.id=(u64)key->num;if(!seen.insert(m.id).second)return fail("Duplicate mate id");editMate(m,j);nd.nextMateId_=std::max(nd.nextMateId_,m.id+1);nd.mates.push_back(m);}}
        catch(const std::exception& e){return fail(e.what());}
    }
    if(auto key=v.find("next_mate_id")){if(key->t!=Value::Num||key->num<(double)nd.nextMateId_||key->num>9007199254740991.||key->num!=std::floor(key->num))return fail("Invalid next mate id");nd.nextMateId_=(u64)key->num;}
    if(nd.params.size()>256||nd.features.size()>1024||nd.sketches.size()>64)return fail("document capacity exceeded");
    nd.refreshParams();
    if(!nd.parameterError.empty())return fail(nd.parameterError);
    std::vector<AssemblyPlacement> placements;std::string assemblyError;if(nd.mates.empty()&&!resolveAssembly(nd,placements,assemblyError))return fail(assemblyError);
    bool supported=false;for(auto& p:nd.planes)supported|=p.support.t==Value::Obj;
    try{if(!supported){for(auto& plane:nd.planes)resolvePlane(nd,plane.name);for(auto& sketch:nd.sketches)resolvePlane(nd,sketch.sk.plane);}}
    catch(const std::exception& e){return fail(e.what());}
    doc=std::move(nd);
    return true;
}

// ---------------------------------------------------------------------------
std::string sketchToJson(const Sketch& sk){
    Value v=Value::makeObj();
    v.set("plane",Value::makeStr(sk.plane));v.set("tolerance",Value::makeNum(sk.tolerance));
    Value es=Value::makeArr(); for(auto&e:sk.ents) es.arr.push_back(entToJson(e));
    Value cs=Value::makeArr(); for(auto&c:sk.cons) cs.arr.push_back(conToJson(c));
    v.set("entities",std::move(es));
    v.set("constraints",std::move(cs));
    return v.dump();
}

bool sketchFromJson(const std::string& json, Sketch& sk, std::string* err){
    Value v;
    if(!arkjson::parse(json,v)){ if(err)*err="malformed JSON"; return false; }
    if(v.t!=Value::Obj){if(err)*err="Sketch must be object";return false;}
    Sketch out;
    if(auto plane=v.find("plane")){if(plane->t!=Value::Str||plane->str.empty()){if(err)*err="Sketch plane must be a name";return false;}out.plane=plane->str;}
    if(auto tolerance=v.find("tolerance")){if(tolerance->t!=Value::Num||!std::isfinite(tolerance->num)||tolerance->num<.0001||tolerance->num>5){if(err)*err="Sketch tolerance must be 0.0001..5 mm";return false;}out.tolerance=tolerance->num;}
    for(auto key:{"entities","constraints"})if(auto list=v.find(key);list&&list->t!=Value::Arr){if(err)*err="Sketch entities/constraints must be arrays";return false;}
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
