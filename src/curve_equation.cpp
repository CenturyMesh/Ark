#include "ark/sketch.hpp"
#include <cmath>
#include <stdexcept>
namespace ark {
arkjson::Value sampleEquationCurve(const arkjson::Value& spec,const ExprEnv& env){
    using V=arkjson::Value;if(spec.t!=V::Obj)throw std::runtime_error("XYZ equation curve requires an object");
    for(const auto& field:spec.obj)if(field.first!="op"&&field.first!="x"&&field.first!="y"&&field.first!="z"&&field.first!="t0"&&field.first!="t1"&&field.first!="samples")throw std::runtime_error("Unknown equation field: "+field.first);
    auto evaluate=[&](const char* key,const ExprEnv& local){const auto* value=spec.find(key);if(!value||(value->t!=V::Str&&value->t!=V::Num)||value->str.size()>2048)throw std::runtime_error(std::string("Equation needs finite numeric or expression ")+key);bool ok=false;const double result=value->t==V::Num?value->num:evalExpr(value->str,local,&ok);if(value->t==V::Num)ok=true;if(!ok||!std::isfinite(result)||std::abs(result)>1e7)throw std::runtime_error(std::string("Invalid equation coordinate/range: ")+key);return result;};
    const double t0=evaluate("t0",env),t1=evaluate("t1",env),samples=evaluate("samples",env);if(t1<=t0||samples<2||samples>512||samples!=std::floor(samples))throw std::runtime_error("Equation needs t1 > t0 and 2..512 interpolation samples");
    V points=V::makeArr();Vec3 prior;for(unsigned i=0;i<(unsigned)samples;++i){ExprEnv local=env;local.vars["t"]=t0+(t1-t0)*i/(samples-1);Vec3 p{evaluate("x",local),evaluate("y",local),evaluate("z",local)};if(i&&(p-prior).len()<1e-8)throw std::runtime_error("Equation has coincident consecutive samples");prior=p;V point=V::makeArr();for(double value:{p.x,p.y,p.z})point.arr.push_back(V::makeNum(value));points.arr.push_back(point);}
    V request=V::makeObj();request.set("op",V::makeStr("curve_interpolate"));request.set("points",points);return request;
}
}
