#include "ark/stock_preview.hpp"
#include "ark/tube.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace ark {
namespace {
using V=arkjson::Value;
const V& required(const V& object,const char* key){const V* value=object.find(key);if(!value)throw std::runtime_error(std::string("Stock preview needs ")+key);return *value;}
std::string named(const V& object,const char* key){const V& value=required(object,key);if(value.t!=V::Str||value.str.empty()||value.str.size()>128)throw std::runtime_error(std::string("Stock preview ")+key+" must be a name");return value.str;}
std::string expression(const V& value){if(value.t==V::Str&&!value.str.empty()&&value.str.size()<=2048)return value.str;if(value.t==V::Num&&std::isfinite(value.num))return value.dump();throw std::runtime_error("Stock profile dimension must be a finite expression");}
double evaluated(const V& value,const ExprEnv& env,const char* label){bool ok=false;const double result=evalExpr(expression(value),env,&ok);if(!ok||!std::isfinite(result)||result<=0||result>1e6)throw std::runtime_error(std::string("Invalid stock ")+label);return result;}
double exactLength(const V& value){if(value.t!=V::Num||!std::isfinite(value.num)||value.num<=0||value.num>1e6)throw std::runtime_error("Stock preview needs a positive evaluated developed length");return value.num;}
std::string lengthExpression(double length){std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(17)<<length;return text.str();}
V baseReport(const Feature& feature){V report=V::makeObj();report.set("feature_id",V::makeNum((double)feature.id));report.set("feature_name",V::makeStr(feature.name));report.set("authority",V::makeStr("display_only"));report.set("length_basis",V::makeStr("centerline"));report.set("manufacturing_qualified",V::makeBool(false));report.set("cope_trim_preserved",V::makeBool(false));report.set("forming_certified",V::makeBool(false));report.set("source_geometry_mutated",V::makeBool(false));return report;}
void appendRow(StockPreview& preview,Feature stock,const ExprEnv& env,const std::string& name,const std::string& profile,double length,double y,V& rows){
    stock.bends.clear();stock.p["tail"]=lengthExpression(length);
    TubeResult result=buildTube(stock,env);
    if(!result.error.empty())throw std::runtime_error("Stock "+name+": "+result.error);
    if(!std::isfinite(result.centerlineLength)||std::abs(result.centerlineLength-length)>std::max(1e-8,length*1e-10))throw std::runtime_error("Stock straight length changed during preview");
    preview.mesh.append(result.mesh,Mat4::translation({-length/2,y,0}));
    V row=V::makeObj();row.set("source_name",V::makeStr(name));row.set("profile",V::makeStr(profile));row.set("developed_length_mm",V::makeNum(length));row.set("row_y_mm",V::makeNum(y));row.set("section_area_mm2",V::makeNum(result.sectionArea));rows.arr.push_back(row);
}
}

StockPreview buildStockPreview(const Feature& feature,const ExprEnv& env,const V& nativeData){
    StockPreview preview;preview.report=baseReport(feature);V rows=V::makeArr();double total=0;
    if(feature.type==FeatureType::Tube){
        const TubeResult source=buildTube(feature,env);if(!source.error.empty())throw std::runtime_error("Tube stock: "+source.error);
        const double length=source.centerlineLength;appendRow(preview,feature,env,feature.name,"tube",length,0,rows);total=length;
    }else if(feature.type==FeatureType::ConstructionFrame){
        const V& report=required(nativeData,"frame_report"),&evaluatedMembers=required(report,"members"),&authoredProfiles=required(feature.nativeSpec,"profiles");
        if(evaluatedMembers.t!=V::Arr||evaluatedMembers.arr.empty()||evaluatedMembers.arr.size()>64||authoredProfiles.t!=V::Arr||authoredProfiles.arr.empty()||authoredProfiles.arr.size()>32)throw std::runtime_error("Frame stock needs evaluated members and authored profiles");
        std::unordered_map<std::string,const V*> profiles;
        for(const V& profile:authoredProfiles.arr){const std::string name=named(profile,"name");if(!profiles.emplace(name,&profile).second)throw std::runtime_error("Duplicate frame stock profile");}
        double rowY=0,previousHalf=0;size_t rowIndex=0;
        for(const V& member:evaluatedMembers.arr){const std::string name=named(member,"name"),profileName=named(member,"profile");const auto found=profiles.find(profileName);if(found==profiles.end())throw std::runtime_error("Stock profile not found for "+name);
            const double length=exactLength(required(member,"developed_length_mm"));const V& authored=*found->second;const std::string section=named(authored,"section");if(section!="round"&&section!="rect")throw std::runtime_error("Stock section must be round or rect");
            Feature stock;stock.type=FeatureType::Tube;stock.p["section"]=section;stock.p["wall"]=expression(required(authored,"wall"));double width=0;
            if(section=="round"){stock.p["od"]=expression(required(authored,"od"));width=evaluated(required(authored,"od"),env,"outer diameter");}
            else{stock.p["width"]=expression(required(authored,"width"));stock.p["height"]=expression(required(authored,"height"));width=evaluated(required(authored,"width"),env,"width");}
            if(rowIndex++)rowY+=previousHalf+width/2+80;previousHalf=width/2;
            appendRow(preview,std::move(stock),env,name,profileName,length,rowY,rows);total+=length;
        }
        const V* reported=report.find("total_developed_length_mm");
        if(reported&&reported->t==V::Num&&std::isfinite(reported->num)&&std::abs(reported->num-total)>std::max(1e-6,total*1e-9))throw std::runtime_error("Frame stock total does not match evaluated member lengths");
    }else throw std::runtime_error("Stock preview supports tube or construction_frame");
    preview.report.set("members",rows);preview.report.set("member_count",V::makeNum((double)rows.arr.size()));preview.report.set("total_developed_length_mm",V::makeNum(total));preview.report.set("triangles",V::makeNum((double)preview.mesh.triCount()));return preview;
}

} // namespace ark
