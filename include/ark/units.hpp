#pragma once
#include "ark/json.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ark {
// Stored geometry stays in mm, physical mass in g and volume in mm^3.
struct DisplayUnits {
    std::string length="mm",mass="g",volume="L";
    int precision=6;
    static double lengthFactor(const std::string& u){if(u=="mm")return 1;if(u=="cm")return 10;if(u=="m")return 1000;if(u=="in")return 25.4;if(u=="ft")return 304.8;throw std::runtime_error("Unknown length unit");}
    static double massFactor(const std::string& u){if(u=="mg")return .001;if(u=="g")return 1;if(u=="kg")return 1000;if(u=="lb")return 453.59237;throw std::runtime_error("Unknown mass unit");}
    static double volumeFactor(const std::string& u){if(u=="mm3")return 1;if(u=="cm3"||u=="mL")return 1000;if(u=="L")return 1000000;if(u=="m3")return 1000000000;throw std::runtime_error("Unknown volume unit");}
    void validate()const{lengthFactor(length);massFactor(mass);volumeFactor(volume);if(precision<3||precision>9)throw std::runtime_error("Precision must be 3..9 significant digits");}
    std::string format(double canonical,double factor,const std::string& symbol)const{if(!std::isfinite(canonical))return "n/a";std::ostringstream s;s<<std::setprecision(precision)<<(canonical==0?0:canonical/factor)<<' '<<symbol;return s.str();}
    std::string distance(double mm)const{return format(mm,lengthFactor(length),length);}
    std::string area(double mm2)const{const auto f=lengthFactor(length);return format(mm2,f*f,length+"2");}
    std::string weight(double grams)const{return format(grams,massFactor(mass),mass);}
    std::string capacity(double mm3)const{return format(mm3,volumeFactor(volume),volume);}
    arkjson::Value json()const{using V=arkjson::Value;V j=V::makeObj();j.set("length",V::makeStr(length));j.set("mass",V::makeStr(mass));j.set("volume",V::makeStr(volume));j.set("precision",V::makeNum(precision));return j;}
    static DisplayUnits fromJson(const arkjson::Value& j){using V=arkjson::Value;DisplayUnits u;for(auto key:{"length","mass","volume"})if(!j.find(key)||j.find(key)->t!=V::Str)throw std::runtime_error("Invalid display units");u.length=j.find("length")->str;u.mass=j.find("mass")->str;u.volume=j.find("volume")->str;if(auto* n=j.find("precision")){if(n->t!=V::Num||!std::isfinite(n->num)||n->num<3||n->num>9||std::floor(n->num)!=n->num)throw std::runtime_error("Invalid unit precision");u.precision=(int)n->num;}u.validate();return u;}
};
}
