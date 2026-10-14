#include "ark/holes_orchestrator.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr const char* metricSource="https://www.lubero.de/de/technik/gewindenormen/metrischesgewinde";
constexpr const char* uncSource="https://www.lubero.de/de/technik/gewindenormen/unc-amerik-grobgewinde";
constexpr const char* unfSource="https://www.lubero.de/de/technik/gewindenormen/unf-amerik-feingewinde";
constexpr const char* clearanceSource="https://cdn.standards.iteh.ai/samples/4183/1a8db2e6de054d2e9bed7d40be64d6e1/ISO-273-1979.pdf";
constexpr const char* shaftSource="https://www.beneri.com/en/prodotto/din-471";
constexpr const char* boreSource="https://www.beneri.com/en/prodotto/din-472";
constexpr const char* unSource="https://www.gewindeschneider-shop.com/gewinde-schneiden/innengewinde/handgewindebohrer/handgewindebohrer-12-un-amerikanisches-un-gewinde-mit-12-gaenge-steigung/";
struct Entry {
    std::string family,size,series,source;
    double nominal=0,pitch=0,tap=0,close=0,normal=0,loose=0,grooveDiameter=0,grooveWidth=0;
};
const V& required(const V& v,const char* key){const auto* p=v.find(key);if(!p)throw std::runtime_error(std::string("Holes Orchestrator requires ")+key);return *p;}
std::string text(const V& v,const char* key,const char* fallback=nullptr){const auto* p=v.find(key);if(!p&&fallback)return fallback;if(!p||p->t!=V::Str||p->str.empty()||p->str.size()>160)throw std::runtime_error(std::string("Invalid Holes Orchestrator text: ")+key);return p->str;}
double number(const V& v,const char* key,double fallback=0){const auto* p=v.find(key);if(!p)return fallback;if(p->t!=V::Num||!std::isfinite(p->num)||std::abs(p->num)>1e6)throw std::runtime_error(std::string("Invalid Holes Orchestrator number: ")+key);return p->num;}
double dimension(const V& v,const char* key,double fallback=0){const double n=number(v,key,fallback);if(n<=1e-6)throw std::runtime_error(std::string("Holes Orchestrator needs positive ")+key+"; select a published catalog value or declare it explicitly");return n;}
void fields(const V& value,const std::set<std::string>& allowed,const char* label){if(value.t!=V::Obj)throw std::runtime_error(std::string(label)+" must be an object");for(const auto& p:value.obj)if(!allowed.count(p.first))throw std::runtime_error(std::string("Unknown ")+label+" field: "+p.first);}
V stringArray(std::initializer_list<const char*> words){V v=V::makeArr();for(auto p:words)v.arr.push_back(V::makeStr(p));return v;}
V type(const char* name){V v=V::makeObj();v.set("type",V::makeStr(name));return v;}
V enumType(std::initializer_list<const char*> words){V v=type("string");v.set("enum",stringArray(words));return v;}
std::string decimal(double n){char buffer[40];std::snprintf(buffer,sizeof buffer,"%.8g",n);return buffer;}

const std::vector<Entry>& entries(){static const std::vector<Entry> data=[](){
    std::vector<Entry> out;
    // Every integral nominal through M50 remains addressable, including special
    // diameters for which a pitch/clearance must be explicitly supplied.
    for(int n=1;n<=50;++n)out.push_back({"metric","M"+std::to_string(n),"M",metricSource,(double)n});
    for(double n:{1.1,1.2,1.4,1.6,1.7,1.8,2.2,2.3,2.5,2.6,3.5,4.5,5.5})out.push_back({"metric","M"+decimal(n),"M",metricSource,n});
    struct Metric{double nominal,pitch,tap;};
    const Metric published[]{
        {1,.25,.75},{1.1,.25,.85},{1.2,.25,.95},{1.4,.3,1.1},{1.6,.35,1.25},{1.7,.35,1.3},{1.8,.35,1.45},
        {2,.4,1.6},{2.2,.45,1.75},{2.3,.4,1.9},{2.5,.45,2.05},{2.6,.45,2.1},{3,.5,2.5},{3.5,.6,2.9},
        {4,.7,3.3},{4.5,.75,3.7},{5,.8,4.2},{5.5,.9,4.6},{6,1,5},{7,1,6},{8,1.25,6.8},{9,1.25,7.8},
        {10,1.5,8.5},{11,1.5,9.5},{12,1.75,10.2},{14,2,12},{16,2,14},{18,2.5,15.5},{20,2.5,17.5},
        {22,2.5,19.5},{24,3,21},{27,3,24},{30,3.5,26.5},{33,3.5,29.5},{36,4,32},{39,4,35},{42,4.5,37.5},
        {45,4.5,40.5},{48,5,43},{50,2,48}
    };
    for(const auto& p:published)for(auto& e:out)if(e.family=="metric"&&std::abs(e.nominal-p.nominal)<1e-10){e.pitch=p.pitch;e.tap=p.tap;}
    // ISO 273 published fine/medium/coarse nominal hole diameters. Missing rows
    // stay missing; special nominal sizes are never silently interpolated.
    struct Clearance{double n,a,b,c;};const Clearance clear[]{
        {1,1.1,1.2,1.3},{1.2,1.3,1.4,1.5},{1.4,1.5,1.6,1.8},{1.6,1.7,1.8,2},{1.8,2,2.1,2.2},{2,2.2,2.4,2.6},
        {2.5,2.7,2.9,3.1},{3,3.2,3.4,3.6},{3.5,3.7,3.9,4.2},{4,4.3,4.5,4.8},{4.5,4.8,5,5.3},{5,5.3,5.5,5.8},
        {6,6.4,6.6,7},{7,7.4,7.6,8},{8,8.4,9,10},{10,10.5,11,12},{12,13,13.5,14.5},{14,15,15.5,16.5},
        {16,17,17.5,18.5},{18,19,20,21},{20,21,22,24},{22,23,24,26},{24,25,26,28},{27,28,30,32},
        {30,31,33,35},{33,34,36,38},{36,37,39,42},{39,40,42,45},{42,43,45,48},{45,46,48,52},{48,50,52,56}
    };
    for(const auto& p:clear)for(auto& e:out)if(e.family=="metric"&&std::abs(e.nominal-p.n)<1e-10){e.close=p.a;e.normal=p.b;e.loose=p.c;}
    struct Inch{const char* size;double inch,coarse,fine,tapCoarse,tapFine;};
    // Pitch counts are manufacturer-published. Inch conversion is exact 25.4,
    // avoiding the source page's rounded (sometimes erroneous) mm conversion.
    const Inch inch[]{
        {"#0",.060,0,80,0,1.25},{"#1",.073,64,72,1.5,1.55},{"#2",.086,56,64,1.8,1.85},{"#3",.099,48,56,2.1,2.1},
        {"#4",.112,40,48,2.3,2.4},{"#5",.125,40,44,2.6,2.7},{"#6",.138,32,40,2.9,3},{"#8",.164,32,36,3.5,3.5},
        {"#10",.190,24,32,3.9,4.1},{"#12",.216,24,28,4.5,4.65},{"1/4",.25,20,28,5.2,5.5},{"5/16",.3125,18,24,6.6,0},
        {"3/8",.375,16,24,8,8.5},{"7/16",.4375,14,20,9.4,9.9},{"1/2",.5,13,20,10.8,11.5},{"9/16",.5625,12,18,12.3,13},
        {"5/8",.625,11,18,13.5,14.5},{"11/16",.6875,11,0,15.2,0},{"3/4",.75,10,16,16.5,17.5},
        {"13/16",.8125,10,0,18.1,0},{"7/8",.875,9,14,19.5,20.5},{"15/16",.9375,9,0,21,0},
        {"1",1,8,12,22.3,23.3},{"1-1/8",1.125,7,12,25,26.5},{"1-1/4",1.25,7,12,28.3,29.5},
        {"1-3/8",1.375,6,12,30.8,32.5},{"1-1/2",1.5,6,12,34,36},{"1-5/8",1.625,6,0,37.1,0},
        {"1-3/4",1.75,5,0,39.5,0},{"1-7/8",1.875,4.5,0,42,0},{"2",2,4.5,0,45,0}
    };
    for(const auto& p:inch){if(p.coarse)out.push_back({"unc",p.size,"UNC",uncSource,p.inch*25.4,25.4/p.coarse,p.tapCoarse});
        if(p.fine)out.push_back({"unf",p.size,"UNF",unfSource,p.inch*25.4,25.4/p.fine,p.tapFine});
        if(p.inch>1.5)out.push_back({"un",p.size,"UN",unSource,p.inch*25.4,25.4/12.0});}
    // The erroneous Lubero UNF5/16 drill >= nominal is intentionally excluded.
    // Guhring's published 6.90 mm preparation is used for that row instead.
    for(auto& e:out)if(e.family=="unf"&&e.size=="5/16"){e.tap=6.9;e.source="https://e-catalogue.guehring.com/frontend/catalogs/1197343/1/pdf/complete.pdf";}
    struct Groove{double n,d,w;};const Groove shaft[]{
        {3,2.8,.5},{4,3.8,.5},{5,4.8,.7},{6,5.7,.8},{7,6.7,.9},{8,7.6,.9},{9,8.6,1.1},{10,9.6,1.1},
        {11,10.5,1.1},{12,11.5,1.1},{13,12.4,1.1},{14,13.4,1.1},{15,14.3,1.1},{16,15.2,1.1},{17,16.2,1.1},
        {18,17,1.3},{19,18,1.3},{20,19,1.3},{21,20,1.3},{22,21,1.3},{23,22,1.3},{24,22.9,1.3},{25,23.9,1.3},
        {26,24.9,1.3},{27,25.6,1.3},{28,26.6,1.6},{29,27.6,1.6},{30,28.6,1.6},{31,29.3,1.6},{32,30.3,1.6},
        {33,31.3,1.6},{34,32.3,1.6},{35,33,1.6},{36,34,1.85},{37,35,1.85},{38,36,1.85},{39,37,1.85},
        {40,37.5,1.85},{41,38.5,1.85},{42,39.5,1.85},{43,40.5,1.85},{44,41.5,1.85},{45,42.5,1.85},
        {46,43.5,1.85},{47,44.5,1.85},{48,45.5,1.85},{50,47,2.15}
    };const Groove bore[]{
        {8,8.4,.9},{9,9.4,.9},{10,10.4,1.1},{11,11.4,1.1},{12,12.5,1.1},{13,13.6,1.1},{14,14.6,1.1},
        {15,15.7,1.1},{16,16.8,1.1},{17,17.8,1.1},{18,19,1.1},{19,20,1.1},{20,21,1.1},{21,22,1.1},
        {22,23,1.1},{23,24.1,1.3},{24,25.2,1.3},{25,26.2,1.3},{26,27.2,1.3},{27,28.4,1.3},{28,29.4,1.3},
        {29,30.4,1.3},{30,31.4,1.3},{31,32.7,1.3},{32,33.7,1.3},{33,34.7,1.3},{34,35.7,1.6},{35,37,1.6},
        {36,38,1.6},{37,39,1.6},{38,40,1.6},{39,41,1.6},{40,42.5,1.85},{41,43.5,1.85},{42,44.5,1.85},
        {43,45.5,1.85},{44,46.5,1.85},{45,47.5,1.85},{46,48.5,1.85},{47,49.5,1.85},{48,50.5,1.85},
        {49,51.5,1.85},{50,53,2.15}
    };
    for(const auto& p:shaft){Entry e{"din471",decimal(p.n),"DIN 471",shaftSource,p.n};e.grooveDiameter=p.d;e.grooveWidth=p.w;out.push_back(e);}
    for(const auto& p:bore){Entry e{"din472",decimal(p.n),"DIN 472",boreSource,p.n};e.grooveDiameter=p.d;e.grooveWidth=p.w;out.push_back(e);}
    for(int n=1;n<=50;++n)out.push_back({"dowel",std::to_string(n),"caller-declared dowel fit","",(double)n});
    std::stable_sort(out.begin(),out.end(),[](const Entry& a,const Entry& b){return a.family==b.family?a.nominal<b.nominal:a.family<b.family;});return out;
}();return data;}

std::string canonical(std::string s){s.erase(std::remove(s.begin(),s.end(),'"'),s.end());std::replace(s.begin(),s.end(),',','.');for(size_t pos;(pos=s.find(' '))!=std::string::npos;)s.replace(pos,1,"-");return s;}
const Entry& lookup(const std::string& family,const std::string& size){const auto normalized=canonical(size);for(const auto& e:entries())if(e.family==family&&e.size==normalized)return e;throw std::runtime_error("Holes Orchestrator catalog has no "+family+" "+size+"; UNC/UNF are series, AISI is a material designation; fine inch sizes above1.5in use family un (12-UN)");}
V entryJson(const Entry& e){V v=V::makeObj();v.set("family",V::makeStr(e.family));v.set("size",V::makeStr(e.size));v.set("nominal_diameter_mm",V::makeNum(e.nominal));v.set("callout_series",V::makeStr(e.series));if(e.pitch)v.set("pitch_mm",V::makeNum(e.pitch));if(e.tap)v.set("tap_preparation_mm",V::makeNum(e.tap));
    if(e.close){V clear=V::makeObj();clear.set("close",V::makeNum(e.close));clear.set("normal",V::makeNum(e.normal));clear.set("loose",V::makeNum(e.loose));v.set("clearance_mm",clear);v.set("clearance_source_url",V::makeStr(clearanceSource));}
    if(e.grooveWidth){v.set("groove_diameter_mm",V::makeNum(e.grooveDiameter));v.set("groove_width_mm",V::makeNum(e.grooveWidth));}
    v.set("dimension_source",V::makeStr(e.source.empty()?"caller-declared nominal; fit limits undeclared":e.family=="metric"&&!e.pitch?"caller-addressable nominal; no primary pitch/preparation default retained":"primary manufacturer published nominal dimensions"));if(!e.source.empty())v.set("source_url",V::makeStr(e.source));
    if(e.family=="metric"&&!e.pitch)v.set("pitch_status",V::makeStr("explicit pitch required; no published default retained"));
    if(e.size=="M50")v.set("pitch_status",V::makeStr("M50x2 fine default; not a coarse-series claim"));
    v.set("tolerance_status",V::makeStr("undeclared; numeric fit/thread/groove tolerance limits are not supplied"));v.set("manufacturing_qualified",V::makeBool(false));return v;}
V vector(const V& v,const char* label){if(v.t!=V::Arr||v.arr.size()!=3)throw std::runtime_error(std::string(label)+" needs3 numbers");for(const auto& n:v.arr)if(n.t!=V::Num||!std::isfinite(n.num)||std::abs(n.num)>1e6)throw std::runtime_error(std::string(label)+" must be expression-evaluated finite mm coordinates");return v;}
}

V holesCatalog(const V& args){fields(args,{"family","size","kind","offset","limit"},"holes_catalog");const std::string family=text(args,"family","all"),size=args.find("size")?text(args,"size"):"";const auto start=number(args,"offset"),limit=number(args,"limit",16);if(start<0||start!=std::floor(start)||limit<1||limit>128||limit!=std::floor(limit))throw std::runtime_error("holes_catalog offset/limit must be bounded integers (limit1..128)");
    if(family!="all"&&family!="metric"&&family!="unc"&&family!="unf"&&family!="un"&&family!="dowel"&&family!="din471"&&family!="din472")throw std::runtime_error("Unknown holes_catalog family; AISI is material, not a thread family");
    const std::string kind=text(args,"kind","all");if(kind!="all"&&kind!="clearance"&&kind!="tapped"&&kind!="drilled"&&kind!="dowel"&&kind!="groove_external"&&kind!="groove_internal")throw std::runtime_error("Unknown holes_catalog kind");
    V out=V::makeObj(),rows=V::makeArr();size_t count=0;for(const auto& e:entries())if((family=="all"||family==e.family)&&(size.empty()||canonical(size)==e.size)){
        if(kind=="dowel"&&e.family!="dowel")continue;if(kind=="groove_external"&&e.family!="din471")continue;if(kind=="groove_internal"&&e.family!="din472")continue;
        if((kind=="tapped"||kind=="clearance"||kind=="drilled")&&(e.family=="dowel"||e.family=="din471"||e.family=="din472"))continue;
        if(count++>=static_cast<size_t>(start)&&rows.arr.size()<static_cast<size_t>(limit)){
            V row=entryJson(e),native=V::makeObj(),recipe=V::makeObj(),p=V::makeObj(),locations=V::makeArr(),location=V::makeObj(),position=V::makeArr(),axis=V::makeArr();
            const bool groove=e.family=="din471"||e.family=="din472";const std::string selected=kind!="all"?kind:e.family=="din471"?"groove_external":e.family=="din472"?"groove_internal":e.family=="dowel"?"dowel":e.pitch?"tapped":"drilled";
            recipe.set("family",V::makeStr(e.family));recipe.set("size",V::makeStr(e.size));recipe.set("kind",V::makeStr(selected));recipe.set("termination",V::makeStr(groove?"blind":"through"));recipe.set("recess",V::makeStr("none"));location.set("name",V::makeStr("location1"));for(double n:{0.,0.,0.})position.arr.push_back(V::makeNum(n));for(double n:{0.,0.,groove?1.:-1.})axis.arr.push_back(V::makeNum(n));location.set("position",position);location.set("direction",axis);locations.arr.push_back(location);recipe.set("points",locations);native.set("op",V::makeStr("holes_orchestrator"));native.set("recipe",recipe);row.set("suggested_native",native);row.set("suggested_p",p);V needed=V::makeArr();if(selected=="tapped"&&!e.pitch)needed.arr.push_back(V::makeStr("p.pitch"));if(selected=="clearance"&&!e.normal)needed.arr.push_back(V::makeStr("p.diameter"));row.set("required_parameters",needed);row.set("placement_required",V::makeStr(groove?"Replace location1 with exact shaft/bore axis and groove axial start; width advances in direction":"Replace location1 with a point on the exact source boundary and an inward direction; attach one source input"));rows.arr.push_back(row);
        }}
    if(!size.empty()&&!count)throw std::runtime_error("No catalog row matches family/size/kind");
    out.set("catalog",V::makeStr("ark-holes-orchestrator-1"));out.set("rows",rows);out.set("total",V::makeNum((double)count));out.set("offset",V::makeNum(start));out.set("has_more",V::makeBool(start+rows.arr.size()<count));out.set("families",stringArray({"metric","unc","unf","un","dowel","din471","din472"}));
    out.set("units",V::makeStr("mm; exact inch conversion25.4"));out.set("geometry",V::makeStr("analytic cylindrical/conical cutters; tapped holes retain semantic threads without a helical surface"));out.set("inch_fine_limit",V::makeStr("Published UNF rows through1.5in; corresponding12-UN rows continue through2in using family un"));out.set("limits",V::makeStr("Missing pitch/clearance/recess/fit limits require explicit caller declarations; no ISO/DIN/ASME certification or manufacturing qualification"));return out;}

V holesCatalogSchema(){V schema=type("object"),p=V::makeObj();p.set("family",enumType({"all","metric","unc","unf","un","dowel","din471","din472"}));p.set("size",type("string"));p.set("kind",enumType({"all","clearance","tapped","drilled","dowel","groove_external","groove_internal"}));auto offset=type("integer");offset.set("minimum",V::makeNum(0));p.set("offset",offset);auto limit=type("integer");limit.set("minimum",V::makeNum(1));limit.set("maximum",V::makeNum(128));p.set("limit",limit);schema.set("properties",p);schema.set("additionalProperties",V::makeBool(false));return schema;}
V holesRecipeSchema(){V schema=type("object"),p=V::makeObj();p.set("family",enumType({"metric","unc","unf","un","dowel","din471","din472"}));p.set("size",type("string"));p.set("kind",enumType({"clearance","tapped","drilled","dowel","groove_external","groove_internal"}));p.set("termination",enumType({"through","blind"}));p.set("fit",enumType({"close","normal","loose"}));p.set("recess",enumType({"none","counterbore","countersink"}));p.set("fit_callout",type("string"));p.set("thread_class",type("string"));
    V scalar=V::makeObj(),choices=V::makeArr();choices.arr.push_back(type("number"));choices.arr.push_back(type("string"));scalar.set("oneOf",choices);V xyz=type("array");xyz.set("items",scalar);xyz.set("minItems",V::makeNum(3));xyz.set("maxItems",V::makeNum(3));V point=type("object"),pp=V::makeObj();pp.set("name",type("string"));pp.set("position",xyz);pp.set("direction",xyz);point.set("properties",pp);point.set("required",stringArray({"name","position","direction"}));point.set("additionalProperties",V::makeBool(false));V points=type("array");points.set("items",point);points.set("minItems",V::makeNum(1));points.set("maxItems",V::makeNum(64));p.set("points",points);schema.set("properties",p);schema.set("required",stringArray({"family","size","kind","points"}));schema.set("additionalProperties",V::makeBool(false));return schema;}
void appendHolesTools(V& tools){V tool=V::makeObj(),annotations=V::makeObj();tool.set("name",V::makeStr("holes_catalog"));tool.set("description",V::makeStr("Read-only Holes Orchestrator nominal catalog: metric throughM50, UNC through2in, UNF through1.5in plus12-UN through2in, dowels, DIN471/472 grooves. Fast local paged lookup; evidence URLs and undeclared tolerance limits. Apply a linked native_solid holes_orchestrator recipe at observed expected_revision."));tool.set("inputSchema",holesCatalogSchema());annotations.set("readOnlyHint",V::makeBool(true));annotations.set("destructiveHint",V::makeBool(false));annotations.set("idempotentHint",V::makeBool(true));tool.set("annotations",annotations);tools.arr.push_back(tool);}

V resolveHolesRecipe(const V& request){fields(request,{"op","recipe","input_brep","input_topology","diameter","depth","pitch","thread_depth","recess_diameter","recess_depth","countersink_angle","groove_diameter","groove_width"},"holes request");const V& recipe=required(request,"recipe");fields(recipe,{"family","size","kind","termination","fit","points","recess","fit_callout","thread_class"},"holes recipe");const auto family=text(recipe,"family"),size=text(recipe,"size"),kind=text(recipe,"kind");const Entry& e=lookup(family,size);
    const bool shaft=kind=="groove_external",bore=kind=="groove_internal",groove=shaft||bore;
    if((shaft&&family!="din471")||(bore&&family!="din472")||(!groove&&(family=="din471"||family=="din472")))throw std::runtime_error("Groove kind must match DIN471 shaft or DIN472 bore family");
    if(kind!="clearance"&&kind!="tapped"&&kind!="drilled"&&kind!="dowel"&&!groove)throw std::runtime_error("Unsupported hole kind");if((kind=="dowel")!=(family=="dowel"))throw std::runtime_error("Dowel kind requires dowel family");if(kind=="tapped"&&(family=="dowel"||groove))throw std::runtime_error("Tapped kind requires a thread family");
    const auto termination=text(recipe,"termination",groove?"blind":"through"),recess=text(recipe,"recess","none"),fit=text(recipe,"fit","normal");if(termination!="through"&&termination!="blind")throw std::runtime_error("Hole termination must be through or blind");if(fit!="close"&&fit!="normal"&&fit!="loose")throw std::runtime_error("Clearance fit must be close, normal or loose");if(recess!="none"&&recess!="counterbore"&&recess!="countersink")throw std::runtime_error("Hole recess must be none, counterbore or countersink");
    if(groove&&(termination!="blind"||recess!="none"||recipe.find("thread_class")||recipe.find("fit_callout")))throw std::runtime_error("Retaining-ring groove has an axial width, no hole termination/recess/thread/fit");
    if(recipe.find("thread_class")&&kind!="tapped")throw std::runtime_error("thread_class requires tapped kind");if(recipe.find("fit_callout")&&kind!="dowel"&&kind!="clearance")throw std::runtime_error("fit_callout requires dowel or clearance kind");
    const auto& points=required(recipe,"points");if(points.t!=V::Arr||points.arr.empty()||points.arr.size()>64)throw std::runtime_error("Holes Orchestrator needs1..64 named points");V locations=V::makeArr();std::set<std::string> names;
    for(const auto& item:points.arr){fields(item,{"name","position","direction"},"hole point");const auto name=text(item,"name");if(name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||!names.insert(name).second)throw std::runtime_error("Hole point names must be unique stable ASCII identifiers");const V position=vector(required(item,"position"),"hole position"),direction=vector(required(item,"direction"),"hole direction");double norm=0;for(const auto& n:direction.arr)norm+=n.num*n.num;if(norm<=1e-20)throw std::runtime_error("Hole direction must be nonzero");V row=V::makeObj();row.set("name",V::makeStr(name));row.set("position",position);V normalized=V::makeArr();for(const auto& n:direction.arr)normalized.arr.push_back(V::makeNum(n.num/std::sqrt(norm)));row.set("direction",normalized);locations.arr.push_back(row);}
    V out=V::makeObj(),report=V::makeObj();out.set("kind",V::makeStr(kind));out.set("termination",V::makeStr(termination));out.set("recess",V::makeStr(recess));out.set("points",locations);out.set("nominal_diameter",V::makeNum(e.nominal));report.set("catalog_entry",entryJson(e));report.set("point_count",V::makeNum((double)locations.arr.size()));report.set("kind",V::makeStr(kind));report.set("termination",V::makeStr(termination));
    report.set("tolerance_status",V::makeStr("undeclared"));report.set("thread_geometry",V::makeStr("semantic callout; no modeled helical flank"));report.set("normative_tolerances_checked",V::makeBool(false));report.set("manufacturing_qualified",V::makeBool(false));
    if(groove){if(request.find("diameter")||request.find("depth")||request.find("pitch")||request.find("thread_depth")||request.find("recess_diameter")||request.find("recess_depth")||request.find("countersink_angle"))throw std::runtime_error("Groove recipe cannot retain hole dimensions");
        const double diameter=dimension(request,"groove_diameter",e.grooveDiameter),width=dimension(request,"groove_width",e.grooveWidth);if(shaft?diameter>=e.nominal:diameter<=e.nominal)throw std::runtime_error("Groove diameter must be below shaft nominal or above bore nominal");out.set("groove_diameter",V::makeNum(diameter));out.set("groove_width",V::makeNum(width));report.set("callout",V::makeStr(e.series+" "+e.size+" groove; dimensional tolerances undeclared"));report.set("groove_diameter_mm",V::makeNum(diameter));report.set("groove_width_mm",V::makeNum(width));report.set("dimension_origin",V::makeStr(request.find("groove_diameter")||request.find("groove_width")?"caller-declared override":"primary manufacturer groove dimensions"));}
    else {
        if(request.find("groove_diameter")||request.find("groove_width"))throw std::runtime_error("Hole recipe cannot retain groove dimensions");
        double pitch=0,defaultDiameter=e.nominal;std::string origin="nominal diameter";
        if(kind=="tapped"){pitch=dimension(request,"pitch",e.pitch);if(pitch>=e.nominal)throw std::runtime_error("Thread pitch must be below nominal diameter");if(family!="metric"&&std::abs(pitch-e.pitch)>1e-10)throw std::runtime_error("Selected Unified series has a fixed catalog pitch; a custom pitch cannot be silently labelled UNC/UNF/12-UN");if(request.find("pitch")&&std::abs(pitch-e.pitch)>1e-10){defaultDiameter=e.nominal-pitch;origin="nominal-minus-pitch preparation estimate; caller must qualify tap/process";}else if(e.tap){defaultDiameter=e.tap;origin="primary manufacturer tap preparation nominal";}else{defaultDiameter=e.nominal-pitch;origin="nominal-minus-pitch preparation estimate; caller must qualify tap/process";}}
        else if(request.find("pitch")||request.find("thread_depth"))throw std::runtime_error("Pitch/thread_depth require tapped kind");
        if(kind=="clearance"){defaultDiameter=fit=="close"?e.close:fit=="loose"?e.loose:e.normal;origin="ISO273 published nominal clearance";if(!defaultDiameter&&!request.find("diameter"))throw std::runtime_error("No published clearance diameter for selected nominal/family; declare p.diameter explicitly");}
        const double diameter=dimension(request,"diameter",defaultDiameter);if(kind=="clearance"&&diameter<=e.nominal)throw std::runtime_error("Clearance diameter must exceed nominal");if(kind=="tapped"&&diameter>=e.nominal)throw std::runtime_error("Tap preparation diameter must be below thread nominal");out.set("diameter",V::makeNum(diameter));report.set("cut_diameter_mm",V::makeNum(diameter));report.set("dimension_origin",V::makeStr(request.find("diameter")?"caller-declared diameter":origin));
        if(termination=="blind")out.set("depth",V::makeNum(dimension(request,"depth")));else if(request.find("depth"))throw std::runtime_error("Through hole derives full source span; remove inactive p.depth");
        if(kind=="tapped"){out.set("pitch",V::makeNum(pitch));std::string callout=family=="metric"?e.size+"x"+decimal(pitch):e.size+"-"+decimal(25.4/pitch)+" "+e.series;if(recipe.find("thread_class")){const auto threadClass=text(recipe,"thread_class");if((family=="metric"&&threadClass!="6H"&&threadClass!="5H"&&threadClass!="4H"&&threadClass!="7H")||(family!="metric"&&threadClass!="1B"&&threadClass!="2B"&&threadClass!="3B"))throw std::runtime_error("Internal thread class must be a supported semantic M H class or Unified B class");callout+=" "+threadClass;report.set("thread_class",V::makeStr(threadClass));report.set("tolerance_status",V::makeStr("caller-declared semantic thread class; numeric limits undeclared"));}
            report.set("callout",V::makeStr(callout));const double td=number(request,"thread_depth",termination=="blind"?number(request,"depth"):0);if(td<0||(termination=="blind"&&td>number(request,"depth")))throw std::runtime_error("Thread depth must be positive and not exceed blind drill depth");if(request.find("thread_depth")&&td<=0)throw std::runtime_error("Explicit thread_depth must be positive");if(td>0)report.set("thread_depth_mm",V::makeNum(td));}
        else{report.set("callout",V::makeStr(kind+" diameter "+decimal(diameter)+" mm"));if(recipe.find("fit_callout")){report.set("fit_callout",V::makeStr(text(recipe,"fit_callout")));report.set("tolerance_status",V::makeStr("caller-declared fit callout; numeric limits undeclared"));}}
        report.set("blind_bottom",V::makeStr("flat; no invented drill tip, runout or tapping allowance"));
        if(recess!="none"){const double rd=dimension(request,"recess_diameter");if(rd<=diameter)throw std::runtime_error("Recess diameter must exceed cut diameter");out.set("recess_diameter",V::makeNum(rd));double recessDepth=0;if(recess=="counterbore"){recessDepth=dimension(request,"recess_depth");if(request.find("countersink_angle"))throw std::runtime_error("Counterbore cannot retain countersink_angle");}
            else{if(request.find("recess_depth"))throw std::runtime_error("Countersink depth derives from diameters and included angle; remove p.recess_depth");const double angle=dimension(request,"countersink_angle");if(angle>=180)throw std::runtime_error("Countersink included angle must be in(0,180) degrees");recessDepth=(rd-diameter)/2/std::tan(angle*3.14159265358979323846/360);out.set("countersink_angle",V::makeNum(angle));}
            if(termination=="blind"&&recessDepth>=number(request,"depth"))throw std::runtime_error("Recess depth must be below blind depth");out.set("recess_depth",V::makeNum(recessDepth));report.set("recess_depth_mm",V::makeNum(recessDepth));report.set("recess_dimension_origin",V::makeStr("caller-declared; not automatically selected from a screw head standard"));}
        else if(request.find("recess_diameter")||request.find("recess_depth")||request.find("countersink_angle"))throw std::runtime_error("No recess selected; remove inactive recess dimensions");
    }
    out.set("report",report);return out;
}
}
