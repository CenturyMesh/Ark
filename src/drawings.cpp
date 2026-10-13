#include "ark/drawings.hpp"
#include "ark/native_features.hpp"
#include "ark/native_backend.hpp"
#include "ark/references.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;namespace fs=std::filesystem;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
const V& need(const V& v,const char* key){auto p=v.find(key);require(p!=nullptr,std::string("Missing drawing field: ")+key);return *p;}
std::string word(const V& v,const char* key){const auto& p=need(v,key);require(p.t==V::Str&&!p.str.empty()&&p.str.size()<=128,std::string("Invalid drawing string: ")+key);return p.str;}
double number(const V& v){require(v.t==V::Num&&std::isfinite(v.num),"Drawing value must be finite numeric data");return v.num;}
void fields(const V& v,std::initializer_list<const char*> allowed){require(v.t==V::Obj,"Drawing data must be an object");for(const auto& entry:v.obj){bool found=false;for(const char* key:allowed)found|=entry.first==key;require(found,"Unknown drawing field: "+entry.first);}}
void xy(const V& v,size_t size){require(v.t==V::Arr&&v.arr.size()==size,"Drawing vector has an invalid size");for(const auto& n:v.arr)require(std::abs(number(n))<=1e6,"Drawing coordinate exceeds 1e6 mm");}
u64 id(const V& v){require(v.t==V::Num&&v.num>=1&&v.num<=9007199254740990.&&v.num==std::floor(v.num),"Drawing feature must be a persistent feature ID");return (u64)v.num;}
u64 featureId(const Document& doc,const V& v){if(v.t==V::Num){u64 key=id(v);require(doc.find(key)!=nullptr,"Drawing source feature not found");return key;}require(v.t==V::Str&&!v.str.empty(),"Drawing source requires feature ID or unique name");u64 found=0;for(const auto& feature:doc.features)if(feature.name==v.str){require(found==0,"Ambiguous drawing source feature");found=feature.id;}require(found!=0,"Drawing source feature not found");return found;}
V& findDrawing(Document& doc,const std::string& name){for(auto& drawing:doc.drawings)if(word(drawing,"name")==name)return drawing;throw std::runtime_error("Drawing not found: "+name);}
Mat4 viewFrame(const V& view){
    const auto direction=word(view,"direction");Vec3 x,y,z;
    if(direction=="front"){x={1,0,0};y={0,0,1};z={0,-1,0};}
    else if(direction=="back"){x={-1,0,0};y={0,0,1};z={0,1,0};}
    else if(direction=="top"){x={1,0,0};y={0,1,0};z={0,0,1};}
    else if(direction=="bottom"){x={1,0,0};y={0,-1,0};z={0,0,-1};}
    else if(direction=="right"){x={0,1,0};y={0,0,1};z={1,0,0};}
    else{x={0,-1,0};y={0,0,1};z={-1,0,0};}
    auto out=Mat4::identity();for(int i=0;i<3;++i){out.m[i]=i==0?x.x:i==1?x.y:x.z;out.m[4+i]=i==0?y.x:i==1?y.y:y.z;out.m[8+i]=i==0?z.x:i==1?z.y:z.z;}
    if(auto origin=view.find("origin_mm"))for(int i=0;i<3;++i)out.m[12+i]=origin->arr[i].num;return out;
}
V matrix(const Mat4& frame){V out=V::makeArr();for(double n:frame.m)out.arr.push_back(V::makeNum(n));return out;}
V pair(double x,double y){V out=V::makeArr();out.arr.push_back(V::makeNum(x));out.arr.push_back(V::makeNum(y));return out;}
Vec2 project(const Mat4& frame,const GeometryReference& ref){Vec3 d{ref.frame.m[12]-frame.m[12],ref.frame.m[13]-frame.m[13],ref.frame.m[14]-frame.m[14]};return {d.dot({frame.m[0],frame.m[1],frame.m[2]}),d.dot({frame.m[4],frame.m[5],frame.m[6]})};}
const V& source(Document& doc,u64 feature){
    const auto* authored=doc.find(feature);require(authored&&!authored->suppressed,"Drawing source is missing or suppressed");
    require(!isMeshModelFeature(authored->type),"Technical drawing source requires an exact solid; mesh geometry is unsupported");
    ensureNativeBody(doc,feature,doc.evalBodies);for(const auto& body:doc.evalBodies)if(body.id==feature){const auto* brep=body.nativeData.find("brep_base64");require(brep&&brep->t==V::Str&&!brep->str.empty(),"Drawing source has no exact B-rep");return *brep;}
    throw std::runtime_error("Drawing source is not evaluated");
}
std::string topologyDigest(const Document& doc,u64 feature){for(const auto& body:doc.evalBodies)if(body.id==feature){const auto* topology=body.nativeData.find("topology");return iterationSha256(topology?topology->dump():"null");}throw std::runtime_error("Drawing source topology is missing");}
V dimensions(Document& doc,const V& drawing){
    V out=V::makeArr();const u64 feature=id(need(drawing,"feature"));const double conversion=word(drawing,"units")=="in"?25.4:1;
    for(const auto& dimension:need(drawing,"dimensions").arr){
        const V* view=nullptr;for(const auto& item:need(drawing,"views").arr)if(word(item,"name")==word(dimension,"view"))view=&item;require(view!=nullptr,"Dimension view is missing");const auto frame=viewFrame(*view);
        auto fraction=[&](const char* key){auto p=dimension.find(key);return p?p->num:.5;};
        auto a=resolveGeometryReference(doc,feature,word(dimension,"reference_a"),fraction("fraction_a"));const auto pa=project(frame,a);const auto kind=word(dimension,"kind");double measured=0;V row=dimension;
        row.set("a_mm",pair(pa.x,pa.y));
        if(kind=="radius"||kind=="diameter"){
            require(a.kind=="axis"&&a.radius>0,"Radius/diameter requires an exact circular axis reference with measured radius");
            const Vec3 axis{a.frame.m[8],a.frame.m[9],a.frame.m[10]},normal{frame.m[8],frame.m[9],frame.m[10]};require(std::abs(axis.dot(normal))>1-1e-9,"Radius/diameter requires a view normal to its circular axis");measured=a.radius*(kind=="diameter"?2:1);row.set("b_mm",pair(pa.x+a.radius,pa.y));
        }else{
            auto b=resolveGeometryReference(doc,feature,word(dimension,"reference_b"),fraction("fraction_b"));const auto pb=project(frame,b);const double dx=pb.x-pa.x,dy=pb.y-pa.y;measured=kind=="horizontal"?std::abs(dx):kind=="vertical"?std::abs(dy):std::hypot(dx,dy);row.set("b_mm",pair(pb.x,pb.y));
        }
        require(measured>1e-9&&std::isfinite(measured),"Drawing dimension collapses in its selected view");row.set("measured_mm",V::makeNum(measured));row.set("value",V::makeNum(measured/conversion));row.set("units",*drawing.find("units"));row.set("authority",V::makeStr("exact_semantic_reference"));row.set("tolerance_authority",V::makeStr("caller_declared"));out.arr.push_back(row);
    }return out;
}
V evaluate(Document& doc,const V& drawing,u64 revision,bool allowUnbound){
    validateDrawing(drawing,!allowUnbound);V report=V::makeObj();report.set("schema",V::makeStr("ark.drawing.v1"));report.set("name",*drawing.find("name"));report.set("revision",V::makeNum((double)revision));report.set("authored",drawing);report.set("manufacturing_qualified",V::makeBool(false));
    const auto feature=id(need(drawing,"feature"));const auto& brep=source(doc,feature);const auto digest=iterationSha256(brep.str),topology=topologyDigest(doc,feature);report.set("current_brep_base64_sha256",V::makeStr(digest));report.set("current_topology_sha256",V::makeStr(topology));
    if(!allowUnbound&&(word(need(drawing,"binding"),"brep_base64_sha256")!=digest||word(need(drawing,"binding"),"topology_sha256")!=topology)){report.set("status",V::makeStr("stale"));report.set("reason",V::makeStr("Source B-rep or semantic topology changed; explicitly drawing.refresh at the current revision before using or exporting measurements"));report.set("measurements_current",V::makeBool(false));return report;}
    size_t derivedBytes=0;V views=V::makeArr();for(const auto& view:need(drawing,"views").arr){V args=V::makeObj();args.set("input_brep",brep);args.set("frame",matrix(viewFrame(view)));args.set("type",*view.find("type"));V request=V::makeObj();request.set("operation",V::makeStr("drawing_view"));request.set("request",args);V result=nativeWorkerRequest(request);result.set("name",*view.find("name"));result.set("authored",view);derivedBytes+=result.dump().size();require(derivedBytes<=16u*1024u*1024u,"Drawing derived views exceed 16 MiB");views.arr.push_back(result);}
    report.set("views",views);report.set("dimensions",dimensions(doc,drawing));report.set("status",V::makeStr("current"));report.set("measurements_current",V::makeBool(true));return report;
}
std::string escaped(const std::string& text){std::string out;for(char c:text){if(c=='&')out+="&amp;";else if(c=='<')out+="&lt;";else if(c=='>')out+="&gt;";else if(c=='\"')out+="&quot;";else out+=c;}return out;}
std::string svg(const V& report){
    const auto& authored=need(report,"authored");const auto& paper=need(authored,"paper_mm");const double scale=number(need(authored,"scale"));std::ostringstream s;s<<std::setprecision(12);s<<"<svg xmlns=\"http://www.w3.org/2000/svg\" font-family=\"Arial,sans-serif\" width=\""<<paper.arr[0].num<<"mm\" height=\""<<paper.arr[1].num<<"mm\" viewBox=\"0 0 "<<paper.arr[0].num<<' '<<paper.arr[1].num<<"\"><title>"<<escaped(word(authored,"name"))<<"</title><desc>Display preview sampled from exact B-rep curves; declared tolerances are not manufacturing qualification.</desc><rect width=\"100%\" height=\"100%\" fill=\"white\"/><defs><marker id=\"arrow\" markerWidth=\"5\" markerHeight=\"5\" refX=\"4\" refY=\"2.5\" orient=\"auto-start-reverse\"><path d=\"M0,0 L5,2.5 L0,5\" fill=\"none\" stroke=\"#315b82\"/></marker></defs>";
    auto line=[&](double x1,double y1,double x2,double y2,bool arrow){s<<"<line x1=\""<<x1<<"\" y1=\""<<y1<<"\" x2=\""<<x2<<"\" y2=\""<<y2<<"\" stroke=\"#315b82\" stroke-width=\"0.2\""<<(arrow?" marker-start=\"url(#arrow)\" marker-end=\"url(#arrow)\"":"")<<"/>";};
    for(const auto& view:need(report,"views").arr){const auto& layout=need(view,"authored");const auto& offset=need(layout,"offset_mm");for(bool hidden:{true,false})for(const auto& edge:need(view,"edges").arr){if((word(edge,"visibility")=="hidden")!=hidden)continue;s<<"<polyline fill=\"none\" stroke=\"#202730\" stroke-width=\"0.25\""<<(word(edge,"visibility")=="hidden"?" stroke-dasharray=\"2 1\"":"")<<" points=\"";for(const auto& point:need(edge,"display_polyline_mm").arr)s<<offset.arr[0].num+point.arr[0].num*scale<<','<<offset.arr[1].num-point.arr[1].num*scale<<' ';s<<"\"/>";}s<<"<text x=\""<<offset.arr[0].num<<"\" y=\""<<offset.arr[1].num+8<<"\" font-size=\"3\">"<<escaped(word(view,"name"))<<"</text>";}
    for(const auto& d:need(report,"dimensions").arr){const V* view=nullptr;for(const auto& v:need(authored,"views").arr)if(word(v,"name")==word(d,"view"))view=&v;const auto& offset=need(*view,"offset_mm");const auto& a=need(d,"a_mm");const auto& b=need(d,"b_mm");const auto& p=need(d,"position_mm");double ax=offset.arr[0].num+a.arr[0].num*scale,ay=offset.arr[1].num-a.arr[1].num*scale,bx=offset.arr[0].num+b.arr[0].num*scale,by=offset.arr[1].num-b.arr[1].num*scale;const auto kind=word(d,"kind");
        if(kind=="horizontal"){line(ax,ay,ax,p.arr[1].num,false);line(bx,by,bx,p.arr[1].num,false);line(ax,p.arr[1].num,bx,p.arr[1].num,true);}
        else if(kind=="vertical"){line(ax,ay,p.arr[0].num,ay,false);line(bx,by,p.arr[0].num,by,false);line(p.arr[0].num,ay,p.arr[0].num,by,true);}
        else if(kind=="linear"){double dx=bx-ax,dy=by-ay,length=std::hypot(dx,dy),nx=-dy/length,ny=dx/length,shift=(p.arr[0].num-ax)*nx+(p.arr[1].num-ay)*ny;line(ax,ay,ax+nx*shift,ay+ny*shift,false);line(bx,by,bx+nx*shift,by+ny*shift,false);line(ax+nx*shift,ay+ny*shift,bx+nx*shift,by+ny*shift,true);}
        else line(p.arr[0].num,p.arr[1].num,bx,by,true);
        s<<"<text x=\""<<p.arr[0].num<<"\" y=\""<<p.arr[1].num-1<<"\" font-size=\"3\" fill=\"#315b82\">"<<(kind=="radius"?"R ":kind=="diameter"?"DIA ":"")<<std::setprecision(6)<<need(d,"value").num<<' '<<escaped(word(d,"units"));if(const auto* tolerance=d.find("tolerance"))s<<" ["<<need(*tolerance,"lower").num<<", +"<<need(*tolerance,"upper").num<<"]";s<<"</text>";}
    s<<"<text x=\"8\" y=\""<<paper.arr[1].num-8<<"\" font-size=\"3\">"<<escaped(word(authored,"name"))<<" | Scale "<<scale<<" | Revision "<<need(report,"revision").num<<" | Units "<<escaped(word(authored,"units"))<<"</text></svg>";return s.str();
}
}
V drawingAuthoringSchema(){
    V schema;require(arkjson::parse(R"({"type":"object","additionalProperties":false,"required":["name","feature","units","scale","paper_mm","views","dimensions"],"properties":{
      "name":{"type":"string","minLength":1,"maxLength":128},
      "feature":{"oneOf":[{"type":"integer","minimum":1},{"type":"string","minLength":1,"maxLength":128}]},
      "units":{"enum":["mm","in"]},"scale":{"type":"number","exclusiveMinimum":0,"maximum":100},
      "paper_mm":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number","minimum":50,"maximum":2000}},
      "views":{"type":"array","minItems":1,"maxItems":12,"items":{"type":"object","additionalProperties":false,"required":["name","type","direction","offset_mm"],"properties":{
        "name":{"type":"string","minLength":1,"maxLength":128},"type":{"enum":["orthographic","section"]},"direction":{"enum":["front","back","top","bottom","right","left"]},
        "offset_mm":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}},"origin_mm":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"},"description":"Required explicit local cut-plane origin for a section; forbidden for orthographic views."}}}},
      "dimensions":{"type":"array","maxItems":128,"items":{"type":"object","additionalProperties":false,"required":["name","view","kind","reference_a","position_mm"],"properties":{
        "name":{"type":"string","minLength":1,"maxLength":128},"view":{"type":"string","minLength":1,"maxLength":128},"kind":{"enum":["linear","horizontal","vertical","radius","diameter"]},
        "reference_a":{"type":"string","minLength":1,"maxLength":128},"reference_b":{"type":"string","minLength":1,"maxLength":128,"description":"Required second semantic origin for linear/horizontal/vertical; forbidden for radius/diameter."},
        "fraction_a":{"type":"number","minimum":0,"maximum":1},"fraction_b":{"type":"number","minimum":0,"maximum":1},"position_mm":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}},
        "tolerance":{"type":"object","additionalProperties":false,"required":["lower","upper","units"],"properties":{"lower":{"type":"number","maximum":0},"upper":{"type":"number","minimum":0},"units":{"enum":["mm","in"]}}}
      }}}
    }})",schema),"Internal drawing schema is malformed");return schema;
}
void validateDrawing(const V& drawing,bool bound){
    fields(drawing,{"name","feature","units","scale","paper_mm","views","dimensions","binding"});word(drawing,"name");id(need(drawing,"feature"));const auto units=word(drawing,"units");require(units=="mm"||units=="in","Drawing units must be mm or in");const double scale=number(need(drawing,"scale"));require(scale>0&&scale<=100,"Drawing scale must be in (0,100]");xy(need(drawing,"paper_mm"),2);for(const auto& n:need(drawing,"paper_mm").arr)require(n.num>=50&&n.num<=2000,"Drawing paper size must be 50..2000 mm");
    const auto& views=need(drawing,"views");require(views.t==V::Arr&&!views.arr.empty()&&views.arr.size()<=12,"Drawing requires 1..12 views");std::set<std::string> names;
    for(const auto& view:views.arr){fields(view,{"name","type","direction","origin_mm","offset_mm"});require(names.insert(word(view,"name")).second,"Duplicate drawing view name");const auto type=word(view,"type"),direction=word(view,"direction");require(type=="orthographic"||type=="section","Drawing view requires orthographic or section type");require(direction=="front"||direction=="back"||direction=="top"||direction=="bottom"||direction=="right"||direction=="left","Unsupported orthographic direction");xy(need(view,"offset_mm"),2);if(type=="section")xy(need(view,"origin_mm"),3);else require(!view.find("origin_mm"),"Orthographic view origin is fixed to the feature origin");}
    const auto& dimensions=need(drawing,"dimensions");require(dimensions.t==V::Arr&&dimensions.arr.size()<=128,"Drawing permits at most 128 dimensions");std::set<std::string> dimNames;
    for(const auto& dimension:dimensions.arr){fields(dimension,{"name","view","kind","reference_a","reference_b","fraction_a","fraction_b","position_mm","tolerance"});require(dimNames.insert(word(dimension,"name")).second,"Duplicate drawing dimension name");require(names.count(word(dimension,"view"))!=0,"Dimension view not found");word(dimension,"reference_a");const auto kind=word(dimension,"kind");require(kind=="linear"||kind=="horizontal"||kind=="vertical"||kind=="radius"||kind=="diameter","Unsupported drawing dimension kind");if(kind=="radius"||kind=="diameter")require(!dimension.find("reference_b")&&!dimension.find("fraction_b"),"Radial dimensions use one exact circular axis reference");else word(dimension,"reference_b");xy(need(dimension,"position_mm"),2);for(const char* key:{"fraction_a","fraction_b"})if(auto f=dimension.find(key))require(number(*f)>=0&&f->num<=1,"Drawing reference fraction must be in [0,1]");if(auto tolerance=dimension.find("tolerance")){fields(*tolerance,{"lower","upper","units"});require(word(*tolerance,"units")==units,"Tolerance units must match the drawing units");require(number(need(*tolerance,"lower"))<=0&&number(need(*tolerance,"upper"))>=0,"Tolerance lower must be <=0 and upper >=0");}}
    if(bound||drawing.find("binding")){const auto& binding=need(drawing,"binding");fields(binding,{"brep_base64_sha256","topology_sha256","observed_revision"});for(const char* key:{"brep_base64_sha256","topology_sha256"}){const auto hash=word(binding,key);require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid drawing source digest");}const double revision=number(need(binding,"observed_revision"));require(revision>=0&&revision<=9007199254740990.&&revision==std::floor(revision),"Invalid drawing observed revision");}
}
void applyDrawingCommand(Document& doc,const V& command){
    const auto op=word(command,"op");if(op=="drawing.set"){fields(command,{"op","drawing"});V drawing=need(command,"drawing");require(!drawing.find("binding"),"Drawing binding is host-owned; use drawing.refresh");drawing.set("feature",V::makeNum((double)featureId(doc,need(drawing,"feature"))));validateDrawing(drawing,false);const auto name=word(drawing,"name");for(auto& existing:doc.drawings)if(word(existing,"name")==name){existing=drawing;return;}require(doc.drawings.size()<32,"Document permits at most 32 technical drawings");doc.drawings.push_back(drawing);}
    else if(op=="drawing.remove"){fields(command,{"op","name"});const auto name=word(command,"name");auto found=std::find_if(doc.drawings.begin(),doc.drawings.end(),[&](const V& row){return word(row,"name")==name;});require(found!=doc.drawings.end(),"Drawing not found");doc.drawings.erase(found);}
    else if(op=="drawing.refresh"){fields(command,{"op","name"});findDrawing(doc,word(command,"name"));}
    else throw std::runtime_error("Unsupported drawing operation");
}
void refreshDrawing(Document& doc,const std::string& name,u64 observedRevision){
    auto& drawing=findDrawing(doc,name);const auto report=evaluate(doc,drawing,observedRevision,true);V binding=V::makeObj();binding.set("brep_base64_sha256",*report.find("current_brep_base64_sha256"));binding.set("topology_sha256",*report.find("current_topology_sha256"));binding.set("observed_revision",V::makeNum((double)observedRevision));drawing.set("binding",binding);
}
V inspectDrawing(Document& doc,const V& args,u64 revision){
    fields(args,{"name","expected_revision"});if(auto expected=args.find("expected_revision"))require(expected->t==V::Num&&expected->num==(double)revision,"Revision conflict; refresh doc_info before drawing inspection");
    if(!args.find("name")){V out=V::makeObj(),rows=V::makeArr();for(const auto& drawing:doc.drawings){V row=V::makeObj();row.set("name",*drawing.find("name"));row.set("feature",*drawing.find("feature"));row.set("binding",*drawing.find("binding"));rows.arr.push_back(row);}out.set("drawings",rows);out.set("revision",V::makeNum((double)revision));out.set("schema",V::makeStr("ark.drawings.v1"));return out;}
    const auto& drawing=findDrawing(doc,word(args,"name"));try{return evaluate(doc,drawing,revision,false);}catch(const std::exception& error){V out=V::makeObj();out.set("schema",V::makeStr("ark.drawing.v1"));out.set("name",*drawing.find("name"));out.set("revision",V::makeNum((double)revision));out.set("authored",drawing);out.set("status",V::makeStr("invalid"));out.set("reason",V::makeStr(error.what()));out.set("measurements_current",V::makeBool(false));return out;}
}
V exportDrawing(Document& doc,const V& args,u64 revision){
    fields(args,{"name","expected_revision","path","format"});const auto& expected=need(args,"expected_revision");require(expected.t==V::Num&&expected.num==(double)revision,"Revision conflict; inspect current drawing before export");const auto format=word(args,"format");require(format=="json"||format=="svg","Drawing export supports json or svg");const auto& path=need(args,"path");require(path.t==V::Str&&!path.str.empty()&&path.str.size()<=4096,"Drawing export path is invalid");V probe=V::makeObj();probe.set("name",need(args,"name"));const auto report=inspectDrawing(doc,probe,revision);require(word(report,"status")=="current","Drawing export requires current geometry and resolved semantic dimensions");const auto bytes=format=="json"?report.dump():svg(report);require(bytes.size()<=16u*1024u*1024u,"Drawing export exceeds 16 MiB");
    const auto destination=fs::absolute(fs::u8path(path.str)).lexically_normal();require(!fs::exists(destination)&&fs::is_directory(destination.parent_path()),"Drawing destination exists or its parent is unavailable");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();auto staged=destination.parent_path()/(".ark-drawing-"+iterationSha256(bytes+std::to_string(stamp))+".tmp");struct Cleanup{fs::path path;~Cleanup(){std::error_code ignored;fs::remove(path,ignored);}}cleanup{staged};{std::ofstream out(staged,std::ios::binary);require((bool)out,"Cannot stage drawing export");out.write(bytes.data(),(std::streamsize)bytes.size());out.flush();require((bool)out,"Cannot flush drawing export");}std::error_code error;fs::create_hard_link(staged,destination,error);require(!error,"Atomic no-replace drawing publication failed: "+error.message());V receipt=V::makeObj();receipt.set("path",path);receipt.set("format",V::makeStr(format));receipt.set("revision",V::makeNum((double)revision));receipt.set("sha256",V::makeStr(iterationSha256(bytes)));receipt.set("authority",V::makeStr(format=="svg"?"display_samples":"exact_derived_curves_and_semantic_measurements"));receipt.set("document_unchanged",V::makeBool(true));return receipt;
}
}
