#include "ark/design_disciplines.hpp"
#include "ark/agent.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
const V& get(const V& v,const char* key){static V empty;const auto* p=v.find(key);return p?*p:empty;}
[[noreturn]] void fail(const std::string& message){throw std::runtime_error("DESIGN_DISCIPLINE: "+message);}
void require(bool ok,const std::string& message){if(!ok)fail(message);}
void keys(const V& v,std::initializer_list<const char*> allowed){require(v.t==V::Obj,"Expected object");for(const auto& p:v.obj)require(std::any_of(allowed.begin(),allowed.end(),[&](const char* s){return p.first==s;}),"Unknown field: "+p.first);}
std::string text(const V& v,const char* key,bool required=true,size_t limit=128){const auto& p=get(v,key);if(p.isNull()&&!required)return {};require(p.t==V::Str&&(!required||!p.str.empty())&&p.str.size()<=limit,std::string("Invalid text: ")+key);for(unsigned char c:p.str)require(c>=32&&c!=127,"Text cannot contain control characters");return p.str;}
double number(const V& v,const char* key,double fallback=0,double bound=10000){const auto& p=get(v,key);if(p.isNull())return fallback;require(p.t==V::Num&&std::isfinite(p.num)&&std::abs(p.num)<=bound,std::string("Invalid number: ")+key);return p.num;}
u64 identity(const V& v,const char* key){const auto& p=get(v,key);if(p.isNull())return 0;require(p.t==V::Num&&p.num>=1&&p.num<=9007199254740990.&&p.num==std::floor(p.num),std::string("Invalid identity: ")+key);return (u64)p.num;}
const AssemblyNode* node(const Document& d,u64 id){for(const auto& n:d.assembly)if(n.id==id)return &n;return nullptr;}
const std::set<std::string> families{"electrical","electronics","mechanical","bodywork","sheet_metal","plastics"};
V strings(const std::set<std::string>& s){V a=V::makeArr();for(const auto& x:s)a.arr.push_back(V::makeStr(x));return a;}
void appendFamilies(const V& v,std::set<std::string>& out){for(const auto& f:get(v,"families").arr)out.insert(f.str);}
const Feature* sourceFeature(const Document& d,const V& symbol){const auto f=identity(symbol,"feature"),n=identity(symbol,"node");require(!(f&&n),"A symbol binds either a feature or an occurrence");if(n){const auto* occurrence=node(d,n);require(occurrence&&occurrence->feature,"Symbol source occurrence is missing or is a group");const int copy=(int)number(symbol,"copy");require(copy>=0&&copy<=31&&copy==number(symbol,"copy"),"Invalid symbol copy");const auto slots=assemblyPatternCopySlots(d,*occurrence);require(std::find(slots.begin(),slots.end(),copy)!=slots.end(),"Symbol source pattern copy is missing");const auto* result=d.find(assemblyPatternCopyFeature(d,*occurrence,copy));require(result,"Symbol source definition is missing");return result;}require(!symbol.find("copy"),"copy requires node");if(f){const auto* result=d.find(f);require(result,"Symbol source definition is missing");return result;}return nullptr;}
const V& array(const V& v,const char* key,size_t limit){const auto& a=get(v,key);require(a.t==V::Arr&&a.arr.size()<=limit,std::string("Invalid array: ")+key);return a;}
struct Point{double x=0,y=0;};
Point pinPoint(const V& symbol,const V& pin){const double a=number(symbol,"rotation")*K_PI/180;const auto x=number(pin,"x"),y=number(pin,"y");return {number(symbol,"x")+x*std::cos(a)-y*std::sin(a),number(symbol,"y")+x*std::sin(a)+y*std::cos(a)};}
struct SheetIndex{std::map<std::string,const V*> symbols;std::map<std::string,Point> pins;std::map<std::string,std::string> nets;};
std::string endpoint(const V& e){keys(e,{"symbol","pin"});return text(e,"symbol")+"/"+text(e,"pin");}
void identifier(const std::string& s){require(s.find('/')==std::string::npos,"Symbol and pin IDs cannot contain /");}
SheetIndex validateSheet(const Document& d,const V& sheet){
    keys(sheet,{"name","title","width","height","symbols","wires"});text(sheet,"name");text(sheet,"title",false,256);
    const auto w=number(sheet,"width",297,2000),h=number(sheet,"height",210,2000);require(w>=50&&h>=50,"Sheet dimensions must be 50..2000 mm");
    SheetIndex index;std::set<std::string> refs,occurrences;
    for(const auto& s:array(sheet,"symbols",512).arr){
        keys(s,{"id","ref","label","kind","x","y","rotation","width","height","pins","feature","node","copy","part_number","manufacturer","description","bom"});
        if(const auto* bom=s.find("bom"))require(bom->t==V::Bool,"bom must be boolean");
        const auto id=text(s,"id"),ref=text(s,"ref");identifier(id);require(index.symbols.emplace(id,&s).second,"Duplicate symbol ID");require(refs.insert(ref).second,"Duplicate component reference");
        text(s,"label",false,256);text(s,"description",false,1024);text(s,"part_number",false);text(s,"manufacturer",false);
        const auto kind=text(s,"kind");require(kind=="block"||kind=="connector"||kind=="resistor"||kind=="switch"||kind=="ground"||kind=="power","Unsupported schematic symbol kind");
        require(number(s,"x")>=0&&number(s,"x")<=w&&number(s,"y")>=0&&number(s,"y")<=h,"Symbol anchor is outside sheet");
        require(number(s,"width",20)>0&&number(s,"width",20)<=200&&number(s,"height",12)>0&&number(s,"height",12)<=200,"Symbol size outside 0..200 mm");
        const auto rotation=number(s,"rotation");require(rotation==0||rotation==90||rotation==180||rotation==270,"Symbol rotation must be 0,90,180,270");
        sourceFeature(d,s);if(identity(s,"node"))require(occurrences.insert(std::to_string(identity(s,"node"))+":"+std::to_string((int)number(s,"copy"))).second,"One occurrence copy cannot represent two components on a sheet");
        const auto& pins=array(s,"pins",64);require(!pins.arr.empty(),"Symbol needs at least one pin");
        for(const auto& p:pins.arr){keys(p,{"id","label","x","y"});const auto pid=text(p,"id");identifier(pid);text(p,"label",false);const auto at=pinPoint(s,p);require(at.x>=0&&at.x<=w&&at.y>=0&&at.y<=h,"Pin is outside sheet");require(index.pins.emplace(id+"/"+pid,at).second,"Duplicate pin ID");}
    }
    std::set<std::string> wires;
    for(const auto& wire:array(sheet,"wires",2048).arr){
        keys(wire,{"id","net","from","to","points"});require(wires.insert(text(wire,"id")).second,"Duplicate wire ID");const auto net=text(wire,"net"),a=endpoint(get(wire,"from")),b=endpoint(get(wire,"to"));require(a!=b,"Wire endpoints must be distinct");
        for(const auto& p:{a,b}){require(index.pins.count(p),"Wire references missing pin: "+p);auto [it,added]=index.nets.emplace(p,net);require(added||it->second==net,"A pin cannot belong to two different nets");}
        if(wire.find("points"))for(const auto& p:array(wire,"points",64).arr){require(p.t==V::Arr&&p.arr.size()==2,"Wire point requires [x,y]");for(const auto& x:p.arr)require(x.t==V::Num&&std::isfinite(x.num),"Wire points must be finite");require(p.arr[0].num>=0&&p.arr[0].num<=w&&p.arr[1].num>=0&&p.arr[1].num<=h,"Wire point outside sheet");}
    }
    return index;
}
std::string csv(const std::string& s){std::string out="\"";for(char c:s){if(c=='\"')out+='\"';out+=c;}return out+'\"';}
std::string xml(const std::string& s){std::string out;for(char c:s){if(c=='&')out+="&amp;";else if(c=='<')out+="&lt;";else if(c=='>')out+="&gt;";else if(c=='\"')out+="&quot;";else out+=c;}return out;}
std::string format(double x){std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(12)<<x;return o.str();}
void writeExclusive(const std::filesystem::path& path,const std::string& contents){require(!std::filesystem::exists(path),"Export path exists; choose a new file");auto temporary=path;temporary+=".ark-export-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tmp";try{{std::ofstream f(temporary,std::ios::binary);require((bool)f,"Cannot create export");f.write(contents.data(),(std::streamsize)contents.size());f.close();require((bool)f,"Cannot finish export");}std::filesystem::create_hard_link(temporary,path);std::filesystem::remove(temporary);}catch(...){std::error_code ec;std::filesystem::remove(temporary,ec);throw;}}
std::vector<Point> wirePoints(const V& wire,const SheetIndex& index){std::vector<Point> out{index.pins.at(endpoint(get(wire,"from")))};for(const auto& p:get(wire,"points").arr)out.push_back({p.arr[0].num,p.arr[1].num});out.push_back(index.pins.at(endpoint(get(wire,"to"))));return out;}
}

arkjson::Value validateClassification(const V& v){
    keys(v,{"families","tags"});V result=V::makeObj();
    for(const char* key:{"families","tags"}){std::set<std::string> seen;const auto& values=get(v,key);if(!values.isNull()){require(values.t==V::Arr&&values.arr.size()<=(std::string(key)=="families"?6:16),"Too many families/tags");for(const auto& value:values.arr){require(value.t==V::Str&&!value.str.empty()&&value.str.size()<=64,"Invalid family/tag");for(unsigned char c:value.str)require(c>=32&&c!=127,"Invalid family/tag text");if(std::string(key)=="families")require(families.count(value.str),"Unknown family: "+value.str);require(seen.insert(value.str).second,"Duplicate family/tag");}}result.set(key,strings(seen));}
    return result;
}
std::set<std::string> effectiveFamilies(const Document& d,u64 feature,u64 occurrence){
    // Explicit occurrence/group classification overrides the nearest inherited scope.
    std::set<u64> visited;for(u64 id=occurrence;id;){if(!visited.insert(id).second)break;const auto* n=node(d,id);if(!n)break;if(!get(n->classification,"families").arr.empty()){std::set<std::string> result;appendFamilies(n->classification,result);return result;}id=n->parent;}
    std::set<std::string> result;if(const auto* f=d.find(feature))appendFamilies(f->classification,result);return result;
}
bool matchesFamilyFilter(const Document& d,u64 feature,u64 occurrence,const std::set<std::string>& filter){if(filter.empty())return true;const auto f=effectiveFamilies(d,feature,occurrence);if(f.empty())return filter.count("unclassified")!=0;for(const auto& key:filter)if(f.count(key))return true;return false;}
bool requiresDesignDisciplines054B(const Document& d){if(!d.schematics.isNull())return true;for(const auto& f:d.features)if(!f.classification.isNull())return true;for(const auto& n:d.assembly)if(!n.classification.isNull())return true;return false;}
void validateDesignDisciplines(const Document& d){for(const auto& f:d.features)if(!f.classification.isNull())validateClassification(f.classification);for(const auto& n:d.assembly)if(!n.classification.isNull())validateClassification(n.classification);if(!d.schematics.isNull()){require(d.schematics.t==V::Arr&&d.schematics.arr.size()<=32,"At most 32 schematic sheets");std::set<std::string> names;for(const auto& s:d.schematics.arr){validateSheet(d,s);require(names.insert(text(s,"name")).second,"Duplicate schematic name");}}}
const V* findSchematic(const Document& d,const std::string& name){for(const auto& s:d.schematics.arr)if(get(s,"name").asStr()==name)return &s;return nullptr;}
bool applyDesignDisciplineCommand(Document& d,const V& command){
    const auto op=get(command,"op").asStr();
    if(op=="classification.set"){
        keys(command,{"op","feature","node","families","tags"});const auto f=identity(command,"feature"),n=identity(command,"node");require((f!=0)!=(n!=0),"Choose one feature or node");V* destination=nullptr;if(f){auto* feature=d.find(f);require(feature,"Classification feature not found");destination=&feature->classification;}else{for(auto& item:d.assembly)if(item.id==n)destination=&item.classification;require(destination,"Classification node not found");}
        V patch=destination->t==V::Obj?*destination:V::makeObj();for(const char* key:{"families","tags"})if(command.find(key))patch.set(key,get(command,key));*destination=validateClassification(patch);return true;
    }
    if(op=="schematic.set"){
        keys(command,{"op","sheet"});V sheet=get(command,"sheet");validateSheet(d,sheet);if(d.schematics.isNull())d.schematics=V::makeArr();auto it=std::find_if(d.schematics.arr.begin(),d.schematics.arr.end(),[&](const V& s){return get(s,"name").asStr()==get(sheet,"name").asStr();});if(it==d.schematics.arr.end()){require(d.schematics.arr.size()<32,"At most 32 schematic sheets");d.schematics.arr.push_back(sheet);}else *it=sheet;return true;
    }
    if(op=="schematic.remove"){keys(command,{"op","name"});const auto name=text(command,"name");require(findSchematic(d,name),"Schematic not found");auto& a=d.schematics.arr;a.erase(std::remove_if(a.begin(),a.end(),[&](const V& s){return get(s,"name").asStr()==name;}),a.end());return true;}
    return false;
}
V inspectClassifications(const Document& d,u64 revision){V out=V::makeObj(),items=V::makeArr();auto add=[&](u64 f,u64 n,const std::string& name,const V& own){V row=V::makeObj();row.set(n?"node":"feature",V::makeNum((double)(n?n:f)));row.set("name",V::makeStr(name));row.set("classification",own);row.set("effective_families",strings(effectiveFamilies(d,f,n)));items.arr.push_back(row);};for(const auto& f:d.features)add(f.id,0,f.name,f.classification);for(const auto& n:d.assembly)add(n.feature,n.id,n.name,n.classification);out.set("revision",V::makeNum((double)revision));out.set("families",strings(families));out.set("items",items);out.set("inheritance",V::makeStr("nearest classified occurrence/group, then definition; empty is unclassified"));out.set("document_unchanged",V::makeBool(true));return out;}
V inspectSchematic(const Document& d,u64 revision,const std::string& name){
    V out=V::makeObj();out.set("revision",V::makeNum((double)revision));out.set("document_unchanged",V::makeBool(true));
    if(name.empty()){V sheets=V::makeArr();for(const auto& sheet:d.schematics.arr){V row=V::makeObj();row.set("name",get(sheet,"name"));row.set("symbols",V::makeNum((double)get(sheet,"symbols").arr.size()));row.set("wires",V::makeNum((double)get(sheet,"wires").arr.size()));sheets.arr.push_back(row);}out.set("sheets",sheets);return out;}
    const auto* sheet=findSchematic(d,name);require(sheet,"Schematic not found");const auto index=validateSheet(d,*sheet);out.set("sheet",*sheet);V bom=V::makeArr(),pins=V::makeArr(),nets=V::makeArr();std::map<std::string,std::set<std::string>> netPins;
    for(const auto& s:get(*sheet,"symbols").arr){const auto* feature=sourceFeature(d,s);V row=V::makeObj();for(const char* key:{"ref","label","feature","node","copy"})if(s.find(key))row.set(key,get(s,key));row.set("quantity",V::makeNum(1));for(const char* key:{"part_number","manufacturer","description"}){const auto& authored=get(s,key);const auto& part=feature?get(feature->part,key):get(V{},key);if(authored.t==V::Str&&!authored.str.empty())row.set(key,authored);else if(!part.isNull())row.set(key,part);}
        row.set("source",V::makeStr(feature?"linked CAD metadata":"schematic declaration"));if(!s.find("bom")||get(s,"bom").b)bom.arr.push_back(row);
        for(const auto& p:get(s,"pins").arr){const auto key=text(s,"id")+"/"+text(p,"id");auto it=index.nets.find(key);if(it==index.nets.end()){V item=V::makeObj();item.set("symbol",get(s,"id"));item.set("pin",get(p,"id"));pins.arr.push_back(item);}else netPins[it->second].insert(key);}
    }
    for(const auto& entry:netPins){V n=V::makeObj();n.set("name",V::makeStr(entry.first));n.set("pins",strings(entry.second));nets.arr.push_back(n);}out.set("bom",bom);out.set("nets",nets);out.set("unconnected_pins",pins);out.set("connectivity",V::makeStr("Explicit pin references and net labels; line crossings do not create connections"));out.set("electrical_qualified",V::makeBool(false));return out;
}
SchematicDrawing schematicDrawing(const Document& document,const V& sheet){
    const auto index=validateSheet(document,sheet);SchematicDrawing art;
    auto line=[&](Point a,Point b,const char* layer){art.lines.push_back({{a.x,a.y},{b.x,b.y},layer});};
    for(const auto& wire:get(sheet,"wires").arr){const auto points=wirePoints(wire,index);for(size_t i=1;i<points.size();++i)line(points[i-1],points[i],"WIRES");if(points.size()>1)art.labels.push_back({{(points[0].x+points[1].x)/2,(points[0].y+points[1].y)/2-1},text(wire,"net")});}
    for(const auto& s:get(sheet,"symbols").arr){
        const double sw=number(s,"width",20)/2,sh=number(s,"height",12)/2;V point=V::makeObj();
        auto transform=[&](double x,double y){point.set("x",V::makeNum(x));point.set("y",V::makeNum(y));return pinPoint(s,point);};
        auto segment=[&](double x,double y,double xx,double yy){line(transform(x,y),transform(xx,yy),"SYMBOLS");};
        const auto kind=text(s,"kind");
        if(kind=="resistor"){segment(-sw,0,-sw*.65,0);double x=-sw*.65,y=0;for(int i=1;i<=6;++i){double nx=-sw*.65+1.3*sw*i/6,ny=i==6?0:(i%2?sh*.5:-sh*.5);segment(x,y,nx,ny);x=nx;y=ny;}segment(sw*.65,0,sw,0);}
        else if(kind=="switch"){segment(-sw,0,-sw*.5,0);segment(-sw*.5,0,sw*.5,-sh*.6);segment(sw*.5,0,sw,0);}
        else if(kind=="ground"){segment(0,-sh,0,0);segment(-sw,0,sw,0);segment(-sw*.65,sh*.35,sw*.65,sh*.35);segment(-sw*.25,sh*.7,sw*.25,sh*.7);}
        else if(kind=="power"){segment(0,sh,0,-sh);segment(0,-sh,-sw*.5,0);segment(0,-sh,sw*.5,0);}
        else{segment(-sw,-sh,sw,-sh);segment(sw,-sh,sw,sh);segment(sw,sh,-sw,sh);segment(-sw,sh,-sw,-sh);if(kind=="connector")segment(-sw*.6,-sh,-sw*.6,sh);}
        art.labels.push_back({{number(s,"x")-sw,number(s,"y")-sh-2},text(s,"ref")+" "+text(s,"label",false)});
        for(const auto& p:get(s,"pins").arr){const auto at=pinPoint(s,p);art.pins.push_back({{at.x,at.y},text(s,"id"),text(p,"id")});line({at.x-.6,at.y},{at.x+.6,at.y},"PINS");line({at.x,at.y-.6},{at.x,at.y+.6},"PINS");art.labels.push_back({{at.x+1,at.y-1},text(p,"id")});}
    }
    return art;
}
bool callDesignDisciplineTool(AgentSession& session,const std::string& tool,const V& args,V& out){
    if(tool!="classification_info"&&tool!="schematic_inspect"&&tool!="schematic_export")return false;
    if(args.find("expected_revision")){const auto& r=get(args,"expected_revision");require(r.t==V::Num&&r.num==(double)session.revision,"REVISION_CONFLICT: inspect current revision");}
    if(tool=="classification_info"){keys(args,{"feature","node","expected_revision","actor"});const auto f=identity(args,"feature"),n=identity(args,"node");require(!(f&&n),"Choose feature or node");require(!f||session.document.find(f),"Classification feature not found");require(!n||node(session.document,n),"Classification node not found");out=inspectClassifications(session.document,session.revision);if(f||n){V rows=get(out,"items");rows.arr.erase(std::remove_if(rows.arr.begin(),rows.arr.end(),[&](const V& row){return identity(row,f?"feature":"node")!=(f?f:n);}),rows.arr.end());out.set("items",rows);}return true;}
    if(tool=="schematic_inspect"){keys(args,{"name","expected_revision","actor"});out=inspectSchematic(session.document,session.revision,text(args,"name",false));return true;}
    keys(args,{"name","format","path","expected_revision","actor"});require(args.find("expected_revision"),"Export requires expected_revision");const auto name=text(args,"name"),kind=text(args,"format"),path=text(args,"path",true,4096);out=inspectSchematic(session.document,session.revision,name);const auto& sheet=get(out,"sheet");const auto index=validateSheet(session.document,sheet);std::ostringstream data;data.imbue(std::locale::classic());
    if(kind=="bom_csv"){data<<"reference,quantity,part_number,manufacturer,description,source\r\n";for(const auto& row:get(out,"bom").arr)data<<csv(get(row,"ref").asStr())<<",1,"<<csv(get(row,"part_number").asStr())<<","<<csv(get(row,"manufacturer").asStr())<<","<<csv(get(row,"description").asStr())<<","<<csv(get(row,"source").asStr())<<"\r\n";}
    else {
        require(kind=="dxf"||kind=="svg","Export supports dxf, svg, bom_csv");const auto w=number(sheet,"width",297),h=number(sheet,"height",210);
        if(kind=="dxf"){
            data<<"0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1027\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n100\nAcDbSymbolTable\n70\n1\n0\nLTYPE\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n2\nCONTINUOUS\n70\n0\n3\nSolid line\n72\n65\n73\n0\n40\n0\n0\nENDTAB\n0\nTABLE\n2\nLAYER\n100\nAcDbSymbolTable\n70\n5\n";
            for(const char* layer:{"0","SYMBOLS","WIRES","PINS","LABELS"})data<<"0\nLAYER\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n2\n"<<layer<<"\n70\n0\n62\n7\n6\nCONTINUOUS\n";
            data<<"0\nENDTAB\n0\nTABLE\n2\nSTYLE\n100\nAcDbSymbolTable\n70\n1\n0\nSTYLE\n100\nAcDbSymbolTableRecord\n100\nAcDbTextStyleTableRecord\n2\nARK_TEXT_STYLE\n70\n0\n40\n0\n41\n1\n50\n0\n71\n0\n42\n2.5\n3\narial.ttf\n4\n\n0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
        }
        else data<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<w<<"mm\" height=\""<<h<<"mm\" viewBox=\"0 0 "<<w<<" "<<h<<"\"><rect width=\"100%\" height=\"100%\" fill=\"white\"/>";
        auto line=[&](Point a,Point b,const char* layer){if(kind=="dxf")data<<"0\nLINE\n100\nAcDbEntity\n8\n"<<layer<<"\n6\nCONTINUOUS\n100\nAcDbLine\n10\n"<<format(a.x)<<"\n20\n"<<format(h-a.y)<<"\n30\n0\n11\n"<<format(b.x)<<"\n21\n"<<format(h-b.y)<<"\n31\n0\n";else data<<"<path d=\"M"<<a.x<<" "<<a.y<<"L"<<b.x<<" "<<b.y<<"\" stroke=\"#19344a\" stroke-width=\"0.3\" fill=\"none\"/>";};
        auto label=[&](Point p,const std::string& value){if(kind=="dxf")data<<"0\nTEXT\n100\nAcDbEntity\n8\nLABELS\n100\nAcDbText\n10\n"<<format(p.x)<<"\n20\n"<<format(h-p.y)<<"\n30\n0\n40\n2.5\n1\n"<<value<<"\n7\nARK_TEXT_STYLE\n100\nAcDbText\n";else data<<"<text x=\""<<p.x<<"\" y=\""<<p.y<<"\" font-size=\"2.5\" font-family=\"sans-serif\" fill=\"#162b3b\">"<<xml(value)<<"</text>";};
        const auto art=schematicDrawing(session.document,sheet);for(const auto& segment:art.lines)line({segment.a.x,segment.a.y},{segment.b.x,segment.b.y},segment.layer.c_str());for(const auto& item:art.labels)label({item.point.x,item.point.y},item.text);
        if(kind=="dxf")data<<"0\nENDSEC\n0\nEOF\n";else data<<"</svg>";
    }
    const auto bytes=data.str();writeExclusive(std::filesystem::u8path(path),bytes);out=V::makeObj();out.set("path",V::makeStr(path));out.set("format",V::makeStr(kind));out.set("bytes",V::makeNum((double)bytes.size()));out.set("revision",V::makeNum((double)session.revision));out.set("electrical_qualified",V::makeBool(false));return true;
}
void appendDesignDisciplineTools(V& tools){
    auto add=[&](const char* name,const char* description,const char* schema,bool read){V tool=V::makeObj(),input,annotations=V::makeObj();arkjson::parse(schema,input);tool.set("name",V::makeStr(name));tool.set("description",V::makeStr(description));tool.set("inputSchema",input);annotations.set("readOnlyHint",V::makeBool(read));tool.set("annotations",annotations);tools.arr.push_back(tool);};
    add("classification_info","Inspect declared families/tags on definitions and assemblies. Optional feature or node restricts the result to one object. doc_apply classification.set{feature|node,families,tags} requires expected_revision. Families: electrical, electronics, mechanical, bodywork, sheet_metal, plastics. Choose from actual design intent, never invent electrical ratings. Eye mode is a reversible local visibility filter.",R"({"type":"object","properties":{"feature":{"type":"integer","minimum":1},"node":{"type":"integer","minimum":1},"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})",true);
    add("schematic_inspect","Inspect editable 2D electrical sheets, explicit nets, unconnected pins and sheet-specific BOM. No name lists sheets. Author only on request through revision-checked doc_apply schematic.set{sheet:{name,title?,width?,height?,symbols:[{id,ref,label?,kind:block|connector|resistor|switch|ground|power,x,y,rotation?,width?,height?,pins:[{id,label?,x,y}],feature?|node?,copy?,part_number?,manufacturer?,description?,bom?:boolean}],wires:[{id,net,from:{symbol,pin},to:{symbol,pin},points?:[[x,y]]}]}}. schematic.remove{name}. Symbols may bind existing CAD identity or explicit standalone part metadata. bom:false excludes annotations/net symbols from the BOM. Crossings do not connect nets. No electrical safety or circuit simulation qualification.",R"({"type":"object","properties":{"name":{"type":"string"},"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})",true);
    add("schematic_export","Export current requested schematic as editable DXF entities, SVG, or its own BOM CSV. Requires expected_revision and a new output path. Explicit connectivity is retained in .ark; DXF is graphic exchange, not an electrical netlist.",R"({"type":"object","properties":{"name":{"type":"string"},"format":{"enum":["dxf","svg","bom_csv"]},"path":{"type":"string"},"expected_revision":{"type":"integer","minimum":0}},"required":["name","format","path","expected_revision"],"additionalProperties":false})",false);
}
}
