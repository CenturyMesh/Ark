#include "ark/drawings.hpp"
#include "common.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLib.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomConvert.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <TopoDS.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
namespace ark::occt {
namespace {
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
const V& need(const V& v,const char* key){auto p=v.find(key);require(p,std::string("Missing exact drawing field: ")+key);return *p;}
double number(const V& v){require(v.t==V::Num&&std::isfinite(v.num)&&std::abs(v.num)<=1e8,"Drawing geometry requires a finite bounded number");return v.num;}
std::string text(const V& v,const char* key){auto p=v.find(key);require(p&&p->t==V::Str&&!p->str.empty(),std::string("Missing drawing string: ")+key);return p->str;}
occ::handle<Geom_BSplineCurve> spline(const TopoDS_Edge& edge){TopLoc_Location location;double first=0,last=0;auto curve=BRep_Tool::Curve(edge,location,first,last);require(!curve.IsNull()&&std::isfinite(first)&&std::isfinite(last)&&last>first,"Drawing edge has no bounded exact curve");curve=occ::down_cast<Geom_Curve>(curve->Transformed(location.Transformation()));occ::handle<Geom_TrimmedCurve> trim=new Geom_TrimmedCurve(curve,first,last);auto result=GeomConvert::CurveToBSplineCurve(trim);require(!result.IsNull(),"Exact drawing curve cannot be represented as a rational spline");if(result->IsPeriodic())result->SetNotPeriodic();return result;}
double length(const TopoDS_Shape& shape){double total=0;for(const auto& item:shapes(shape,TopAbs_EDGE)){auto edge=TopoDS::Edge(item);if(!BRep_Tool::Degenerated(edge)){BRepAdaptor_Curve curve(edge);total+=GCPnts_AbscissaPoint::Length(curve);}}return total;}
struct Pair {int code;std::string value;};
struct Style {const char* layer;const char* type;int weight;};
Style roleStyle(const std::string& role){
    if(role=="visible")return {"ARK_VISIBLE","CONTINUOUS",25};
    if(role=="hidden")return {"ARK_HIDDEN","DASHED",15};
    if(role=="section")return {"ARK_SECTION","CONTINUOUS",25};
    if(role=="hatch")return {"ARK_HATCH","CONTINUOUS",13};
    if(role=="centreline")return {"ARK_CENTRE","CENTER",13};
    throw std::runtime_error("DRAWING_DXF_UNSUPPORTED: unknown exact edge role");
}
struct StagedDrawing {
    fs::path directory,path;
    explicit StagedDrawing(const fs::path& destination){
        std::random_device random;
        for(int attempt=0;attempt<100;++attempt){
            std::string token=".ark-drawing-";for(int i=0;i<16;++i){unsigned byte=random()&255;token+="0123456789abcdef"[byte>>4];token+="0123456789abcdef"[byte&15];}
            directory=destination.parent_path()/token;std::error_code error;
            if(fs::create_directory(directory,error)){path=directory/"pending.dxf";return;}
            require(!error,"Drawing staging directory cannot be created");
        }
        throw std::runtime_error("Drawing staging reservation failed");
    }
    ~StagedDrawing(){std::error_code ignored;fs::remove(path,ignored);fs::remove(directory,ignored);}
};
using Record=std::vector<Pair>;
void subclasses(const Record& record,const std::vector<std::string>& expected){
    std::vector<std::string> actual;std::string active;
    for(const auto& p:record){
        if(p.code==100){actual.push_back(p.value);active=p.value;}
        else if((p.code==8||p.code==6)&&!record.empty()&&(record[0].value=="SPLINE"||record[0].value=="LINE"||record[0].value=="TEXT"))
            require(active=="AcDbEntity","DXF common attributes precede their AcDbEntity subclass");
    }
    require(actual==expected,"DXF readback changed or omitted subclass boundaries");
}
std::string numeric(double v){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<v;return out.str();}
void pair(std::ostringstream& out,int code,const std::string& value){require(value.find_first_of("\r\n")==std::string::npos,"DXF value contains a newline");out<<code<<'\n'<<value<<'\n';}
void pair(std::ostringstream& out,int code,double value){pair(out,code,numeric(value));}
void sourceData(std::ostringstream& out,const std::string& metadata){
    require(metadata.size()<=8192,"DRAWING_DXF_CAPACITY: source metadata exceeds 8192 bytes");pair(out,1001,"ARK_SOURCE");
    for(size_t i=0;i<metadata.size();){size_t end=std::min(i+240,metadata.size());while(end<metadata.size()&&end>i&&((unsigned char)metadata[end]&0xc0)==0x80)--end;require(end>i,"DXF invalid source UTF-8 chunk");pair(out,1000,metadata.substr(i,end-i));i=end;}
}
std::string sourceData(const Record& record){bool app=false;std::string result;for(const auto& item:record){if(item.code==1001){require(!app&&item.value=="ARK_SOURCE","DXF source application changed");app=true;}else if(item.code==1000){require(app,"DXF source data has no application");result+=item.value;}}require(app&&!result.empty(),"DXF source ownership missing");return result;}
void lineStyleTables(std::ostringstream& out){
    pair(out,0,"SECTION");pair(out,2,"TABLES");pair(out,0,"TABLE");pair(out,2,"LTYPE");pair(out,100,"AcDbSymbolTable");pair(out,70,3.);
    for(const std::string name:{"CONTINUOUS","DASHED","CENTER"}){
        pair(out,0,"LTYPE");pair(out,100,"AcDbSymbolTableRecord");pair(out,100,"AcDbLinetypeTableRecord");pair(out,2,name);pair(out,70,0.);pair(out,3,"ARK canonical drawing style");pair(out,72,65.);
        const std::vector<double> pattern=name=="DASHED"?std::vector<double>{2,-1}:name=="CENTER"?std::vector<double>{4,-1,.5,-1}:std::vector<double>{};double total=0;for(double v:pattern)total+=std::abs(v);pair(out,73,(double)pattern.size());pair(out,40,total);for(double v:pattern){pair(out,49,v);pair(out,74,0.);}
    }
    pair(out,0,"ENDTAB");pair(out,0,"TABLE");pair(out,2,"LAYER");pair(out,100,"AcDbSymbolTable");pair(out,70,8.);
    for(const std::string name:{"0","ARK_VISIBLE","ARK_HIDDEN","ARK_SECTION","ARK_HATCH","ARK_CENTRE","ARK_DIM","ARK_TEXT"}){pair(out,0,"LAYER");pair(out,100,"AcDbSymbolTableRecord");pair(out,100,"AcDbLayerTableRecord");pair(out,2,name);pair(out,70,0.);pair(out,62,7.);pair(out,6,name=="ARK_HIDDEN"?"DASHED":name=="ARK_CENTRE"?"CENTER":"CONTINUOUS");}
    pair(out,0,"ENDTAB");pair(out,0,"TABLE");pair(out,2,"APPID");pair(out,100,"AcDbSymbolTable");pair(out,70,1.);pair(out,0,"APPID");pair(out,100,"AcDbSymbolTableRecord");pair(out,100,"AcDbRegAppTableRecord");pair(out,2,"ARK_SOURCE");pair(out,70,0.);pair(out,0,"ENDTAB");
    pair(out,0,"TABLE");pair(out,2,"STYLE");pair(out,100,"AcDbSymbolTable");pair(out,70,1.);pair(out,0,"STYLE");pair(out,100,"AcDbSymbolTableRecord");pair(out,100,"AcDbTextStyleTableRecord");pair(out,2,"ARK_TEXT_STYLE");pair(out,70,0.);pair(out,40,0.);pair(out,41,1.);pair(out,50,0.);pair(out,71,0.);pair(out,42,3.);pair(out,3,"arial.ttf");pair(out,4,"");pair(out,0,"ENDTAB");pair(out,0,"ENDSEC");
}
void writeSpline(std::ostringstream& out,const occ::handle<Geom_BSplineCurve>& curve,const std::string& role,const std::string& metadata,int color){
    const auto style=roleStyle(role);pair(out,0,"SPLINE");pair(out,100,"AcDbEntity");pair(out,8,style.layer);pair(out,6,style.type);pair(out,370,(double)style.weight);pair(out,420,(double)color);pair(out,100,"AcDbSpline");pair(out,210,0.);pair(out,220,0.);pair(out,230,1.);pair(out,70,(double)(8|(curve->IsRational()?4:0)|(curve->IsClosed()?1:0)));pair(out,71,(double)curve->Degree());int knots=0;for(int i=1;i<=curve->NbKnots();++i)knots+=curve->Multiplicity(i);pair(out,72,(double)knots);pair(out,73,(double)curve->NbPoles());pair(out,74,0.);for(int i=1;i<=curve->NbKnots();++i)for(int n=0;n<curve->Multiplicity(i);++n)pair(out,40,curve->Knot(i));for(int i=1;i<=curve->NbPoles();++i)pair(out,41,curve->Weight(i));for(int i=1;i<=curve->NbPoles();++i){const auto p=curve->Pole(i);pair(out,10,p.X());pair(out,20,p.Y());pair(out,30,p.Z());}sourceData(out,metadata);
}
std::vector<Record> readRecords(const std::string& bytes,const std::string& sectionName="ENTITIES"){std::istringstream stream(bytes);stream.imbue(std::locale::classic());std::vector<Record> result;std::string codeLine,value;bool entities=false,section=false;while(std::getline(stream,codeLine)){require((bool)std::getline(stream,value),"DXF readback has an incomplete code/value pair");if(!codeLine.empty()&&codeLine.back()=='\r')codeLine.pop_back();if(!value.empty()&&value.back()=='\r')value.pop_back();size_t used=0;int code=std::stoi(codeLine,&used);require(used==codeLine.size(),"DXF readback has invalid group code");if(code==0&&value=="SECTION"){section=true;continue;}if(section&&code==2){entities=value==sectionName;section=false;continue;}if(code==0&&value=="ENDSEC"){entities=false;continue;}if(!entities)continue;if(code==0)result.push_back({});require(!result.empty(),"DXF entity properties precede entity");result.back().push_back({code,value});}return result;}
double value(const Record& record,int code){const Pair* found=nullptr;for(const auto& p:record)if(p.code==code){require(!found,"DXF readback has a duplicate scalar");found=&p;}require(found,"DXF readback missing scalar");size_t used=0;double n=std::stod(found->value,&used);require(used==found->value.size()&&std::isfinite(n),"DXF readback non-finite scalar");return n;}
std::vector<double> values(const Record& record,int code){std::vector<double> out;for(const auto& p:record)if(p.code==code){size_t used=0;double n=std::stod(p.value,&used);require(used==p.value.size()&&std::isfinite(n),"DXF readback invalid curve scalar");out.push_back(n);}return out;}
std::string word(const Record& record,int code){const Pair* found=nullptr;for(const auto& p:record)if(p.code==code){require(!found,"DXF duplicate string attribute");found=&p;}require(found,"DXF missing string attribute");return found->value;}
occ::handle<Geom_BSplineCurve> readSpline(const Record& record){auto flat=values(record,40),weights=values(record,41),xs=values(record,10),ys=values(record,20),zs=values(record,30);int degree=(int)value(record,71),flags=(int)value(record,70);require(degree>=1&&degree<=25&&(flags&2)==0&&flat.size()==value(record,72)&&xs.size()==value(record,73)&&xs.size()==ys.size()&&xs.size()==zs.size()&&xs.size()==weights.size()&&xs.size()>=2,"DXF spline control inventory differs from declared counts");std::vector<double> knots;std::vector<int> multiplicities;for(double n:flat){if(knots.empty()||n!=knots.back()){require(knots.empty()||n>knots.back(),"DXF spline knots not ordered");knots.push_back(n);multiplicities.push_back(1);}else ++multiplicities.back();}require(knots.size()>=2,"DXF spline missing knot domain");TColgp_Array1OfPnt poles(1,(int)xs.size());TColStd_Array1OfReal w(1,(int)weights.size()),k(1,(int)knots.size());TColStd_Array1OfInteger m(1,(int)knots.size());for(int i=1;i<=poles.Length();++i){poles.SetValue(i,gp_Pnt(xs[i-1],ys[i-1],zs[i-1]));w.SetValue(i,weights[i-1]);}for(int i=1;i<=k.Length();++i){k.SetValue(i,knots[i-1]);m.SetValue(i,multiplicities[i-1]);}auto result=occ::handle<Geom_BSplineCurve>(new Geom_BSplineCurve(poles,w,k,m,degree,false));require((flags&~13)==0&&(flags&8)!=0&&((flags&1)!=0)==result->IsClosed()&&((flags&4)!=0)==result->IsRational()&&value(record,210)==0&&value(record,220)==0&&value(record,230)==1,"DXF spline closed/rational/plane flags changed");return result;}
double controlError(const occ::handle<Geom_BSplineCurve>& a,const occ::handle<Geom_BSplineCurve>& b){require(a->Degree()==b->Degree()&&a->NbPoles()==b->NbPoles()&&a->NbKnots()==b->NbKnots(),"DXF spline structure changed");double error=0;for(int i=1;i<=a->NbPoles();++i){error=std::max(error,a->Pole(i).Distance(b->Pole(i)));error=std::max(error,std::abs(a->Weight(i)-b->Weight(i)));}for(int i=1;i<=a->NbKnots();++i){require(a->Multiplicity(i)==b->Multiplicity(i),"DXF spline multiplicity changed");error=std::max(error,std::abs(a->Knot(i)-b->Knot(i)));}return error;}
void styleReadback(const std::string& bytes){
    size_t types=0,layers=0;bool app=false,font=false;std::set<std::string> names;
    for(const auto& row:readRecords(bytes,"TABLES")){
        if(row.empty())continue;const auto kind=row[0].value;
        if(kind=="LTYPE"){subclasses(row,{"AcDbSymbolTableRecord","AcDbLinetypeTableRecord"});const auto name=word(row,2);const auto pattern=name=="DASHED"?std::vector<double>{2,-1}:name=="CENTER"?std::vector<double>{4,-1,.5,-1}:std::vector<double>{};double total=0;for(double v:pattern)total+=std::abs(v);require(value(row,73)==pattern.size()&&value(row,40)==total&&values(row,49)==pattern,"DXF linetype pattern changed");++types;}
        else if(kind=="LAYER"){subclasses(row,{"AcDbSymbolTableRecord","AcDbLayerTableRecord"});const auto name=word(row,2);require(names.insert(name).second,"DXF duplicate layer");require(word(row,6)==(name=="ARK_HIDDEN"?"DASHED":name=="ARK_CENTRE"?"CENTER":"CONTINUOUS"),"DXF layer linetype changed");++layers;}
        else if(kind=="APPID"){subclasses(row,{"AcDbSymbolTableRecord","AcDbRegAppTableRecord"});app=word(row,2)=="ARK_SOURCE";}
        else if(kind=="STYLE"){subclasses(row,{"AcDbSymbolTableRecord","AcDbTextStyleTableRecord"});font=word(row,2)=="ARK_TEXT_STYLE"&&word(row,3)=="arial.ttf";}
        else if(kind=="TABLE")subclasses(row,{"AcDbSymbolTable"});else require(kind=="ENDTAB","DXF unsupported table record");
    }
    require(types==3&&layers==8&&names.count("0")&&app&&font,"DXF style/source tables missing");
}
}
V drawingDxfNative(const V& request){
    const auto path=fs::u8path(text(request,"path"));require(path.extension()==".dxf"&&!fs::exists(path)&&fs::is_directory(path.parent_path()),"DRAWING_DXF_PATH: requires a new .dxf output path and existing parent");
    const auto& report=need(request,"report");require(text(report,"status")=="current","DRAWING_DXF_STALE: requires current canonical drawing geometry");
    const auto& presentation=need(report,"presentation"),paper=need(presentation,"paper_mm"),views=need(report,"views");const double scale=number(need(presentation,"scale")),height=number(paper.arr.at(1));require(scale>0&&views.t==V::Arr&&views.arr.size()<=16,"DRAWING_DXF_CAPACITY: invalid canonical views");
    const auto* recipe=presentation.find("template");const bool blueprint=recipe&&text(*recipe,"id")=="ark_blueprint";const int ink=blueprint?0xcee7f6:0x25394b,fine=blueprint?0x628fb1:0x99afbd,dimInk=blueprint?0x8dd9f3:0x306b92;
    std::ostringstream out;out.imbue(std::locale::classic());pair(out,0,"SECTION");pair(out,2,"HEADER");pair(out,9,"$ACADVER");pair(out,1,"AC1027");pair(out,9,"$INSUNITS");pair(out,70,4.);pair(out,0,"ENDSEC");lineStyleTables(out);pair(out,0,"SECTION");pair(out,2,"ENTITIES");
    std::vector<occ::handle<Geom_BSplineCurve>> curves;std::vector<std::string> roles,metadata;std::vector<int> colors;std::vector<TopoDS_Shape> original,converted;V counts=V::makeObj();for(const char* role:{"visible","hidden","section","hatch","centreline"})counts.set(role,V::makeNum(0));
    double conversionBounds=0,conversionLength=0;V sourceInventory=V::makeArr();
    for(const auto& view:views.arr){
        const auto& layout=need(view,"authored"),offset=need(layout,"offset_mm");gp_Trsf placement;placement.SetScale(gp_Pnt(0,0,0),scale);placement.SetTranslationPart(gp_Vec(number(offset.arr.at(0)),height-number(offset.arr.at(1)),0));
        for(const char* group:{"edges","hatches","centrelines"}){
            const auto& rows=need(view,group);require(rows.t==V::Arr,"DRAWING_DXF_UNSUPPORTED: canonical curve collection must be an array");
            for(const auto& row:rows.arr){
                const auto role=text(row,"visibility");roleStyle(role);require((std::string(group)=="edges"&&(role=="visible"||role=="hidden"||role=="section"))||(std::string(group)=="hatches"&&role=="hatch")||(std::string(group)=="centrelines"&&role=="centreline"),"DRAWING_DXF_UNSUPPORTED: role does not match canonical collection");
                const auto shape=transformed(readBrep(text(row,"exact_brep_base64")),placement);const auto edges=shapes(shape,TopAbs_EDGE);require(edges.size()==1&&shapes(shape,TopAbs_FACE).empty(),"DRAWING_DXF_UNSUPPORTED: canonical curve must contain exactly one edge");const auto edge=TopoDS::Edge(edges.front());require(!BRep_Tool::Degenerated(edge),"DRAWING_DXF_UNSUPPORTED: degenerate exact edge");const auto curve=spline(edge);require(curves.size()<16384&&curve->NbPoles()<=16384,"DRAWING_DXF_CAPACITY: exact curve inventory exceeds bound");const auto exact=BRepBuilderAPI_MakeEdge(curve).Edge();const auto a=bounds(shape),b=bounds(exact);for(size_t i=0;i<a.size();++i)conversionBounds=std::max(conversionBounds,std::abs(a[i]-b[i]));conversionLength=std::max(conversionLength,std::abs(length(shape)-length(exact)));require(std::max(std::abs(a[2]),std::abs(a[5]))<=1e-6,"DRAWING_DXF_UNSUPPORTED: projected curve is not planar");
                V tag=V::makeObj();tag.set("drawing",need(report,"name"));tag.set("view",need(view,"name"));tag.set("source",need(row,"source"));tag.set("curve_id",need(row,"curve_id"));tag.set("role",V::makeStr(role));tag.set("stroke_mm",V::makeNum(role=="hatch"?.12:role=="centreline"?.14:role=="hidden"?.15:.25));const auto encoded=tag.dump();const int color=role=="hidden"||role=="hatch"?fine:role=="centreline"?dimInk:ink;
                writeSpline(out,curve,role,encoded,color);curves.push_back(curve);roles.push_back(role);metadata.push_back(encoded);colors.push_back(color);original.push_back(shape);converted.push_back(exact);sourceInventory.arr.push_back(tag);counts.set(role.c_str(),V::makeNum(need(counts,role.c_str()).num+1));
            }
        }
    }
    require(!curves.empty()&&conversionBounds<=1e-6&&conversionLength<=1e-6,"DRAWING_DXF_CONVERSION_LOSS: exact rational conversion failed bounds/length checks");
    const auto& annotations=need(request,"annotations");require(annotations.t==V::Arr&&annotations.arr.size()<=8192,"DRAWING_DXF_CAPACITY: annotations require bounded array");size_t lines=0,texts=0;
    for(const auto& item:annotations.arr){
        const auto kind=text(item,"kind");pair(out,0,kind=="line"?"LINE":"TEXT");pair(out,100,"AcDbEntity");pair(out,8,kind=="line"?"ARK_DIM":"ARK_TEXT");pair(out,6,"CONTINUOUS");pair(out,370,20.);pair(out,420,number(need(item,"color")));
        if(kind=="line"){const auto& a=need(item,"a"),b=need(item,"b");require(a.t==V::Arr&&a.arr.size()==2&&b.t==V::Arr&&b.arr.size()==2,"DRAWING_DXF_UNSUPPORTED: annotation line requires two 2D points");pair(out,100,"AcDbLine");pair(out,10,number(a.arr[0]));pair(out,20,height-number(a.arr[1]));pair(out,30,0.);pair(out,11,number(b.arr[0]));pair(out,21,height-number(b.arr[1]));pair(out,31,0.);++lines;}
        else {require(kind=="text","DRAWING_DXF_UNSUPPORTED: annotation kind");const auto& at=need(item,"at");require(at.t==V::Arr&&at.arr.size()==2,"DXF text requires 2D location");const auto& labelValue=need(item,"text");require(labelValue.t==V::Str,"DXF text must be text");const auto& label=labelValue.str;require(label.find_first_of("\r\n")==std::string::npos,"DRAWING_DXF_UNSUPPORTED: multiline text requires an explicit supported representation");pair(out,100,"AcDbText");pair(out,10,number(at.arr[0]));pair(out,20,height-number(at.arr[1]));pair(out,30,0.);pair(out,40,number(need(item,"height")));pair(out,1,label);pair(out,7,"ARK_TEXT_STYLE");pair(out,100,"AcDbText");++texts;}
        sourceData(out,item.dump());
    }
    pair(out,0,"ENDSEC");pair(out,0,"EOF");require(out.str().size()<=16u*1024u*1024u,"DRAWING_DXF_CAPACITY: export exceeds 16 MiB");StagedDrawing staged(path);writeFile(staged.path,out.str());const auto readback=readFile(staged.path);styleReadback(readback);const auto records=readRecords(readback);size_t splineCount=0,lineCount=0,textCount=0,annotationIndex=0;double maxControl=0,maxDeviation=0;std::vector<TopoDS_Shape> returned;
    for(const auto& row:records){
        require(!row.empty()&&row[0].code==0,"DXF invalid readback entity");const auto kind=row[0].value;subclasses(row,kind=="SPLINE"?std::vector<std::string>{"AcDbEntity","AcDbSpline"}:kind=="LINE"?std::vector<std::string>{"AcDbEntity","AcDbLine"}:std::vector<std::string>{"AcDbEntity","AcDbText","AcDbText"});
        if(kind=="SPLINE"){
            require(splineCount<curves.size(),"DXF added an unexpected exact curve");const auto style=roleStyle(roles[splineCount]);require(word(row,8)==style.layer&&word(row,6)==style.type&&value(row,370)==style.weight&&value(row,420)==colors[splineCount],"DXF exact edge style changed");require(sourceData(row)==metadata[splineCount],"DXF exact edge source ownership changed");const auto curve=readSpline(row),before=curves[splineCount++];maxControl=std::max(maxControl,controlError(before,curve));for(int i=0;i<=32;++i){const double t=(double)i/32;maxDeviation=std::max(maxDeviation,before->Value(before->FirstParameter()+(before->LastParameter()-before->FirstParameter())*t).Distance(curve->Value(curve->FirstParameter()+(curve->LastParameter()-curve->FirstParameter())*t)));}returned.push_back(BRepBuilderAPI_MakeEdge(curve).Edge());
        }else {
            require(annotationIndex<annotations.arr.size(),"DXF added an annotation");const auto& expected=annotations.arr[annotationIndex++];require(sourceData(row)==expected.dump()&&value(row,420)==number(need(expected,"color")),"DXF annotation source/style changed");
            if(kind=="LINE"){require(text(expected,"kind")=="line"&&word(row,8)=="ARK_DIM","DXF annotation line type changed");const auto& a=need(expected,"a"),b=need(expected,"b");require(value(row,10)==number(a.arr[0])&&value(row,20)==height-number(a.arr[1])&&value(row,11)==number(b.arr[0])&&value(row,21)==height-number(b.arr[1]),"DXF dimension line changed");++lineCount;}
            else {require(kind=="TEXT"&&text(expected,"kind")=="text"&&word(row,8)=="ARK_TEXT"&&word(row,7)=="ARK_TEXT_STYLE","DXF unsupported annotation");const auto& at=need(expected,"at");require(value(row,10)==number(at.arr[0])&&value(row,20)==height-number(at.arr[1])&&value(row,40)==number(need(expected,"height"))&&word(row,1)==need(expected,"text").str,"DXF text placement/content changed");++textCount;}
        }
    }
    require(splineCount==curves.size()&&lineCount==lines&&textCount==texts&&annotationIndex==annotations.arr.size(),"DXF readback entity inventory differs");require(maxControl<=1e-12&&maxDeviation<=1e-9,"DRAWING_DXF_READBACK_LOSS: spline controls or evaluated positions changed");const auto before=compound(converted),after=compound(returned);const auto a=bounds(before),b=bounds(after);double boundsError=0;for(size_t i=0;i<a.size();++i)boundsError=std::max(boundsError,std::abs(a[i]-b[i]));const double lengthError=std::abs(length(before)-length(after));require(boundsError<=1e-9&&lengthError<=1e-8,"DRAWING_DXF_READBACK_LOSS: curve bounds or length changed");
    std::error_code error;fs::create_hard_link(staged.path,path,error);require(!error,"DRAWING_DXF_PUBLICATION_FAILED: atomic no-replace publication failed; destination preserved");
    V result=V::makeObj();result.set("file_sha256",V::makeStr(sha256(readback)));result.set("units",V::makeStr("mm"));result.set("exact_curve_entities",V::makeNum((double)splineCount));result.set("curve_roles",counts);result.set("dimension_line_entities",V::makeNum((double)lineCount));result.set("text_entities",V::makeNum((double)textCount));result.set("source_ownership_sha256",V::makeStr(sha256(sourceInventory.dump())));result.set("source_ownership_preserved",V::makeBool(true));result.set("hidden_and_centre_styles_preserved",V::makeBool(true));result.set("source_conversion_bounds_error_mm",V::makeNum(conversionBounds));result.set("source_conversion_length_error_mm",V::makeNum(conversionLength));result.set("dxf_roundtrip_max_control_error",V::makeNum(maxControl));result.set("dxf_roundtrip_max_deviation_mm",V::makeNum(maxDeviation));result.set("dxf_roundtrip_bounds_error_mm",V::makeNum(boundsError));result.set("dxf_roundtrip_length_error_mm",V::makeNum(lengthError));result.set("geometry_authority",V::makeStr("exact rational B-spline DXF curves; no polyline reconstruction"));result.set("readback_scope",V::makeStr("independent DXF group parser reconstructs all rational splines; verifies controls/knots/weights, 33 parameter samples, length/bounds, roles/source metadata and every line/text annotation; no third-party consumer qualification"));result.set("presentation_limits",V::makeStr("DXF Y-up paper coordinates in mm; SVG page background omitted; hatch 0.12mm and centreline 0.14mm strokes use standard 0.13mm DXF lineweight with exact requested stroke retained in ARK_SOURCE XDATA; Arial font availability/metrics unqualified"));result.set("dimension_entities_associative",V::makeBool(false));result.set("ark_authoring_history_preserved",V::makeBool(false));result.set("ark_drawing_binding_preserved",V::makeBool(false));return result;
}
}
