#include "ark/model.hpp"
#include <set>
namespace ark {
bool resolveAssembly(const Document& d,std::vector<AssemblyPlacement>& placements,std::string& error){
    placements.clear();error.clear();
    auto fail=[&](const std::string& e){error=e;placements.clear();return false;};
    if(d.assembly.size()>4096)return fail("Assembly capacity exceeds 4096 nodes");
    std::unordered_map<u64,const AssemblyNode*> byId;
    std::unordered_map<u64,Mat4> world;
    std::unordered_map<u64,bool> visibility;
    std::set<u64> features;for(auto& f:d.features)features.insert(f.id);
    for(auto& n:d.assembly){
        if(!n.id||!byId.emplace(n.id,&n).second)return fail("Duplicate/zero assembly ID");
        if(n.feature&&!features.count(n.feature))return fail("Assembly references missing feature #"+std::to_string(n.feature));
    }
    ExprEnv env=d.buildEnv();std::set<u64> active;
    std::function<bool(u64,int)> resolve=[&](u64 id,int depth){
        if(world.count(id))return true;
        if(depth>64||active.count(id)){error="Assembly cycle or depth exceeds 64";return false;}
        auto found=byId.find(id);if(found==byId.end()){error="Assembly parent not found";return false;}
        auto& n=*found->second;active.insert(id);Mat4 parent;
        if(n.parent){auto p=byId.find(n.parent);if(p==byId.end()||p->second->feature){error="Parent must be an assembly group";return false;}
            if(!resolve(n.parent,depth+1))return false;
            parent=world[n.parent];}
        for(auto& kv:n.p)if(kv.first!="tx"&&kv.first!="ty"&&kv.first!="tz"&&kv.first!="rx"&&kv.first!="ry"&&kv.first!="rz"){error="Unsupported assembly transform: "+kv.first;return false;}
        auto number=[&](const char* key){auto p=n.p.find(key);if(p==n.p.end())return 0.;bool ok=false;f64 value=evalExpr(p->second,env,&ok);if(!ok||std::fabs(value)>1e6)error=std::string("Invalid assembly expression: ")+key;return value;};
        Vec3 t{number("tx"),number("ty"),number("tz")};f64 rx=number("rx"),ry=number("ry"),rz=number("rz");
        if(!error.empty())return false;
        world[id]=parent*Mat4::translation(t)*Mat4::rotZ(deg2rad(rz))*Mat4::rotY(deg2rad(ry))*Mat4::rotX(deg2rad(rx));
        visibility[id]=n.visible&&(!n.parent||visibility[n.parent]);active.erase(id);return true;
    };
    for(auto& n:d.assembly)if(!resolve(n.id,0)){placements.clear();return false;}
    for(auto& n:d.assembly)if(n.feature)placements.push_back({n.id,n.feature,n.name,world[n.id],visibility[n.id]});
    return true;
}
}
