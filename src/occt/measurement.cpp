#include "common.hpp"
#include "ark/occt_solid.hpp"
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <Precision.hxx>
#include <gp_Lin.hxx>
#include <cmath>
#include <stdexcept>

namespace ark::occt {
arkjson::Value measurementPickNative(const arkjson::Value& request){
    using V=arkjson::Value;
    auto point=[&](const char* key){auto* v=request.find(key);if(!v||v->t!=V::Arr||v->arr.size()!=3)throw std::runtime_error("Measurement requires three coordinates");for(auto& n:v->arr)if(n.t!=V::Num||!std::isfinite(n.num))throw std::runtime_error("Nonfinite measurement coordinate");return gp_Pnt(v->arr[0].num,v->arr[1].num,v->arr[2].num);};
    auto* brep=request.find("brep");if(!brep||brep->t!=V::Str)throw std::runtime_error("Measurement requires exact B-rep");
    auto shape=readBrep(brep->str);auto origin=point("origin"),vector=point("direction");gp_Vec direction(vector.X(),vector.Y(),vector.Z());if(direction.Magnitude()<1e-12)throw std::runtime_error("Measurement ray has no direction");
    double snap=0;if(auto* v=request.find("snap_radius_mm")){if(v->t!=V::Num||!std::isfinite(v->num)||v->num<0)throw std::runtime_error("Invalid snap acquisition radius");snap=v->num;}
    gp_Lin ray(origin,gp_Dir(direction));IntCurvesFace_ShapeIntersector hit;hit.Load(shape,Precision::Confusion());hit.Perform(ray,0,1e100);
    if(!hit.IsDone()||!hit.NbPnt())throw std::runtime_error("No exact surface intersection; click inside the body silhouette");
    int closest=1;for(int i=2;i<=hit.NbPnt();++i)if(hit.WParameter(i)<hit.WParameter(closest))closest=i;
    auto selected=hit.Pnt(closest);bool vertex=false;double best=snap;
    // This radius only chooses an exact vertex; it never alters B-rep tolerance.
    for(const auto& item:shapes(shape,TopAbs_VERTEX)){auto p=BRep_Tool::Pnt(TopoDS::Vertex(item));double d=ray.Distance(p);if(d<best&&p.Distance(hit.Pnt(closest))<=snap*2&&gp_Vec(origin,p).Dot(gp_Vec(ray.Direction()))<=hit.WParameter(closest)+Precision::Confusion()){best=d;selected=p;vertex=true;}}
    V out=V::makeObj(),xyz=V::makeArr();for(auto n:{selected.X(),selected.Y(),selected.Z()})xyz.arr.push_back(V::makeNum(n));out.set("point_mm",xyz);out.set("authority",V::makeStr("exact_brep"));out.set("snap",V::makeStr(vertex?"Vertex":"Surface point"));return out;
}
}
