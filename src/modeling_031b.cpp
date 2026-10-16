#include "ark/modeling_031b.hpp"
#include "ark/references.hpp"
#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/electronics.hpp"
#include "ark/mesh_collision.hpp"
#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>
namespace ark {
using V=arkjson::Value;
namespace {
const V& required031(const V& o,const char* k){auto p=o.find(k);if(!p)throw std::runtime_error(std::string("Missing 031B field: ")+k);return *p;}
double scalar031(const V& v,const ExprEnv& env){bool ok=v.t==V::Num;double n=v.num;if(v.t==V::Str)n=evalExpr(v.str,env,&ok);if(!ok||!std::isfinite(n)||std::abs(n)>1e8)throw std::runtime_error("031B value must be a finite bounded number or expression");return n;}
Vec3 point031(const V& v,const ExprEnv& env){if(v.t!=V::Arr||v.arr.size()!=3)throw std::runtime_error("031B point requires three coordinates");return {scalar031(v.arr[0],env),scalar031(v.arr[1],env),scalar031(v.arr[2],env)};}
V xyz031(Vec3 p){V a=V::makeArr();for(double n:{p.x,p.y,p.z})a.arr.push_back(V::makeNum(n));return a;}
u64 node031(const Document& d,const V& value){if(value.t==V::Num&&value.num>=1&&value.num<=9007199254740990.&&value.num==std::floor(value.num)){for(const auto& n:d.assembly)if(n.id==(u64)value.num)return n.id;}else if(value.t==V::Str){u64 id=0;for(const auto& n:d.assembly)if(n.name==value.str){if(id)throw std::runtime_error("Ambiguous 031B occurrence name");id=n.id;}if(id)return id;}throw std::runtime_error("Missing 031B occurrence");}
bool newRecipe031(const Feature& f){
 const auto* operation=f.nativeSpec.find("op");std::string op=operation?operation->asStr():f.get("op");
 // NativeSolid evaluation gives the retained parameter op precedence.
 if(f.type==FeatureType::NativeSolid&&!f.get("op").empty())op=f.get("op");
 if(f.type==FeatureType::NativeSolid){static const std::set<std::string> ops{"rib","gusset","boss","mount_pad","screw_support","nut_seat","lid_lip","lid_groove","split_surface","split_tool"};if(ops.count(op)||f.nativeSpec.find("radius_law"))return true;if(op=="chamfer"){if(f.nativeSpec.find("reference_face")||f.nativeSpec.find("angle")||f.p.count("angle"))return true;if(auto m=f.nativeSpec.find("mode");m&&m->asStr()!="symmetric")return true;}}
 if(f.type==FeatureType::SurfaceFill&&(f.nativeSpec.find("constraints")||op=="blend"))return true;
 if(f.type==FeatureType::SurfaceEdit&&(op=="extend"||op=="untrim"||op=="mutual_trim"))return true;
 if(f.type==FeatureType::Sweep&&(f.nativeSpec.find("orientation")||f.nativeSpec.find("laws")))return true;
 if(f.type==FeatureType::MeshEdit){static const std::set<std::string> ops{"remesh","simplify","fill_nonplanar","bridge","shrinkwrap"};if(ops.count(op))return true;}
 if(f.type==FeatureType::CableRoute){std::function<bool(const V&)> bound=[&](const V& x){if(x.t==V::Obj){if(x.find("node"))return true;for(const auto& kv:x.obj)if(bound(kv.second))return true;}if(x.t==V::Arr)for(const auto& y:x.arr)if(bound(y))return true;return false;};if(bound(f.nativeSpec))return true;}
 return false;
}
}
bool modeling031BNeedsVersion43(const Document& d){for(const auto& f:d.features)if(newRecipe031(f))return true;return false;}
#include "modeling_031b_tools.inl"
#include "assembly_sweep_031b.inl"
}
