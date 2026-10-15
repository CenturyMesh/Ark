#include "ark/agent.hpp"
#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include "ark/occt_modeling.hpp"
#include "ark/version.hpp"
#include "../src/occt/common.hpp"
#include <BRepCheck_Analyzer.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_List.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using V=arkjson::Value;
namespace fs=std::filesystem;
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
const V& need(const V& v,const char* key){const auto* p=v.find(key);check(p,std::string("Missing corpus field: ")+key);return *p;}
std::string bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);check((bool)input,"Cannot read corpus fixture");return {std::istreambuf_iterator<char>(input),{}};}
V parse(const std::string& source){V v;check(arkjson::parse(source,v),"Malformed corpus JSON");return v;}
double expression(const ark::Document& d,const V& v){if(v.t==V::Num)return v.num;check(v.t==V::Str,"Corpus expected expression is not numeric/text");bool ok=false;const double result=ark::evalExpr(v.str,d.buildEnv(),&ok);check(ok&&std::isfinite(result),"Unresolved corpus expectation: "+v.str);return result;}
void assertNear(double value,double expected,double tolerance,const std::string& why){check(std::isfinite(value)&&std::isfinite(expected)&&std::abs(value-expected)<=tolerance,why+": observed="+V::makeNum(value).dump()+", expected="+V::makeNum(expected).dump()+", tolerance="+V::makeNum(tolerance).dump());}
const V& path(const V& source,const std::string& selector){const V* current=&source;std::istringstream stream(selector);std::string key;while(std::getline(stream,key,'.')){if(current->t==V::Arr){check(!key.empty()&&key.find_first_not_of("0123456789")==std::string::npos,"Invalid array report selector");const auto index=(size_t)std::stoull(key);check(index<current->arr.size(),"Report array index missing: "+selector);current=&current->arr[index];}else current=current->find(key.c_str());check(current,"Missing measured report field: "+selector);}return *current;}
const ark::Body& body(const ark::Document& d,const std::string& name){const ark::Feature* selected=nullptr;for(const auto& f:d.features)if(f.name==name){check(!selected,"Ambiguous corpus feature name");selected=&f;}check(selected,"Corpus check feature missing: "+name);for(const auto& b:d.evalBodies)if(b.id==selected->id)return b;throw std::runtime_error("Corpus feature not evaluated: "+name);}
std::string evaluatedDigest(const ark::Document& d){std::string source;for(const auto& b:d.evalBodies){source+=std::to_string(b.id)+":";if(const auto* p=b.nativeData.find("brep_base64"))source+=p->str;else source+="MISSING_EXACT_AUTHORITY";}return ark::iterationSha256(source);}
V exactInventory(const ark::Document& d){V rows=V::makeArr();for(const auto& b:d.evalBodies){const auto* f=d.find(b.id);check(f&&!ark::isMeshModelFeature(f->type),"Native corpus acquired mesh authority");const auto& encoded=need(b.nativeData,"brep_base64");check(encoded.t==V::Str&&!encoded.str.empty(),"Native corpus result has no B-rep");const auto shape=ark::occt::readBrep(encoded.str);check(BRepCheck_Analyzer(shape).IsValid(),"Native corpus result failed independent B-rep validation");V row=V::makeObj();row.set("feature",V::makeStr(f->name));row.set("solids",V::makeNum((double)ark::occt::shapes(shape,TopAbs_SOLID).size()));row.set("area_mm2",V::makeNum(ark::occt::properties(shape,'a')));row.set("volume_mm3",ark::occt::shapes(shape,TopAbs_SOLID).empty()?V{}:V::makeNum(ark::occt::properties(shape)));row.set("brep_base64_sha256",V::makeStr(ark::iterationSha256(encoded.str)));row.set("brep_valid",V::makeBool(true));rows.arr.push_back(row);}return rows;}
V measuredChecks(const ark::Document& d,const V& checks){
    V results=V::makeArr();for(const auto& item:checks.arr){const auto name=need(item,"feature").str;const auto& b=body(d,name);const auto shape=ark::occt::readBrep(need(b.nativeData,"brep_base64").str);const double volume=ark::occt::properties(shape),area=ark::occt::properties(shape,'a');const auto bounds=ark::occt::bounds(shape);const double tolerance=item.find("tolerance")?expression(d,*item.find("tolerance")):1e-6;
        if(const auto* wanted=item.find("solids"))check(ark::occt::shapes(shape,TopAbs_SOLID).size()==(size_t)wanted->num,"Wrong native solid count: "+name);
        for(const auto& property:{std::pair{"volume",volume},std::pair{"area",area},std::pair{"height",bounds[5]-bounds[2]}}){if(const auto* expected=item.find(property.first))assertNear(property.second,expression(d,*expected),tolerance,name+" "+property.first);if(const auto* minimum=item.find((std::string(property.first)+"_min").c_str()))check(property.second>expression(d,*minimum),name+" below explicit "+property.first+" bound");if(const auto* maximum=item.find((std::string(property.first)+"_max").c_str()))check(property.second<expression(d,*maximum),name+" above explicit "+property.first+" bound");}
        if(const auto* expected=item.find("bounds")){check(expected->t==V::Arr&&expected->arr.size()==6,"Expected six exact bounds");for(size_t i=0;i<6;++i)assertNear(bounds[i],expression(d,expected->arr[i]),tolerance,name+" bound "+std::to_string(i));}
        V row=V::makeObj();row.set("feature",V::makeStr(name));row.set("area_mm2",V::makeNum(area));row.set("volume_mm3",ark::occt::shapes(shape,TopAbs_SOLID).empty()?V{}:V::makeNum(volume));V box=V::makeArr();for(double value:bounds)box.arr.push_back(V::makeNum(value));row.set("bounds_mm",box);V metrics=V::makeArr();
        if(const auto* rules=item.find("report"))for(const auto& rule:rules->arr){const auto& actual=path(need(b.nativeData,"report"),need(rule,"path").str);if(const auto* expected=rule.find("equals"))check(actual.dump()==expected->dump(),name+" report equality failed: "+need(rule,"path").str);if(const auto* maximum=rule.find("max"))check(actual.t==V::Num&&actual.num<=expression(d,*maximum),name+" measured report exceeds fixed bound");if(const auto* expected=rule.find("expr"))assertNear(actual.num,expression(d,*expected),1e-6,name+" native report expression");V metric=V::makeObj();metric.set("path",need(rule,"path"));metric.set("observed",actual);metrics.arr.push_back(metric);}row.set("report_metrics",metrics);results.arr.push_back(row);
    }return results;
}
void persistence(const ark::Document& d){const auto authored=ark::documentToJson(d);ark::Document restored;std::string error;check(ark::documentFromJson(authored,restored,&error),"Corpus authoring readback failed: "+error);check(ark::documentToJson(restored)==authored,"Native authoring readback changed recipes/history");restored.recompute();for(const auto& result:restored.lastResults)check(result.ok,"Native readback recomputation failed: "+result.error);const auto before=exactInventory(d),after=exactInventory(restored);check(before.arr.size()==after.arr.size(),"Native readback changed evaluated body inventory");for(size_t i=0;i<before.arr.size();++i){check(need(before.arr[i],"feature").str==need(after.arr[i],"feature").str,"Native readback changed definition identity");for(const char* property:{"area_mm2","volume_mm3"})if(need(before.arr[i],property).t==V::Num)assertNear(need(after.arr[i],property).num,need(before.arr[i],property).num,std::max(1e-7,std::abs(need(before.arr[i],property).num)*1e-9),"Native readback property changed");}
    bool repair=false;for(const auto& feature:d.features)if(feature.type==ark::FeatureType::SurfaceEdit){const auto* op=feature.nativeSpec.find("op");repair|=op&&op->asStr()=="repair";}
    if(repair){auto downgraded=parse(authored);check(need(downgraded,"version").num>=41,"Repair recipe did not select conditional source version 41");downgraded.set("version",V::makeNum(40));ark::Document forbidden;check(!ark::documentFromJson(downgraded.dump(),forbidden,&error),"Repair accepted an older source format without its contract");}
}
int periodicTrimProbe(){
    V result=V::makeObj(),halves=V::makeArr();auto cylinder=BRepPrimAPI_MakeCylinder(8,20).Shape();TopoDS_Shape side;
    for(const auto& f:ark::occt::shapes(cylinder,TopAbs_FACE))if(BRepAdaptor_Surface(TopoDS::Face(f)).GetType()==GeomAbs_Cylinder)side=f;
    check(!side.IsNull(),"Probe cylinder side missing");result.set("source_area_mm2",V::makeNum(ark::occt::properties(side,'a')));
    const gp_Pnt origin(0,0,6);BRepBuilderAPI_MakeFace plane(gp_Pln(origin,gp_Dir(0,0,1)));
    for(double sign:{1.,-1.}){
        BRepPrimAPI_MakeHalfSpace region(plane.Face(),origin.Translated(gp_Vec(0,0,sign)));BRepAlgoAPI_Common cut(side,region.Solid());cut.Build();V row=V::makeObj();row.set("sign",V::makeNum(sign));row.set("done",V::makeBool(cut.IsDone()));row.set("errors",V::makeBool(cut.HasErrors()));
        auto shape=cut.Shape();row.set("shape_type",V::makeNum((int)shape.ShapeType()));row.set("faces",V::makeNum((double)ark::occt::shapes(shape,TopAbs_FACE).size()));row.set("edges",V::makeNum((double)ark::occt::shapes(shape,TopAbs_EDGE).size()));row.set("area_mm2",V::makeNum(ark::occt::properties(shape,'a')));row.set("valid",V::makeBool(BRepCheck_Analyzer(shape).IsValid()));
        V faces=V::makeArr();for(const auto& f:ark::occt::shapes(shape,TopAbs_FACE)){V item=V::makeObj();BRepAdaptor_Surface support(TopoDS::Face(f));item.set("support_type",V::makeNum((int)support.GetType()));item.set("first_u",V::makeNum(support.FirstUParameter()));item.set("last_u",V::makeNum(support.LastUParameter()));item.set("first_v",V::makeNum(support.FirstVParameter()));item.set("last_v",V::makeNum(support.LastVParameter()));item.set("area_mm2",V::makeNum(ark::occt::properties(f,'a')));faces.arr.push_back(item);}row.set("inventory",faces);
        ShapeUpgrade_UnifySameDomain unify(shape,true,true,false);unify.Build();auto unified=unify.Shape();row.set("unified_faces",V::makeNum((double)ark::occt::shapes(unified,TopAbs_FACE).size()));row.set("unified_area_mm2",V::makeNum(ark::occt::properties(unified,'a')));row.set("unified_valid",V::makeBool(BRepCheck_Analyzer(unified).IsValid()));halves.arr.push_back(row);
    }
    result.set("halves",halves);
    NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(side);tools.Append(plane.Face());BRepAlgoAPI_Splitter split;split.SetArguments(arguments);split.SetTools(tools);split.Build();V splitResult=V::makeObj(),pieces=V::makeArr();splitResult.set("done",V::makeBool(split.IsDone()));splitResult.set("errors",V::makeBool(split.HasErrors()));
    for(const auto& f:ark::occt::shapes(split.Shape(),TopAbs_FACE)){GProp_GProps props;BRepGProp::SurfaceProperties(f,props);V item=V::makeObj();item.set("area_mm2",V::makeNum(props.Mass()));item.set("centroid_z",V::makeNum(props.CentreOfMass().Z()));item.set("valid",V::makeBool(BRepCheck_Analyzer(f).IsValid()));pieces.arr.push_back(item);}splitResult.set("pieces",pieces);result.set("splitter",splitResult);
    std::cout<<result.dump()<<'\n';return 0;
}
V planeTrimRegressions(){
    V rows=V::makeArr();const auto cylinder=BRepPrimAPI_MakeCylinder(8,20).Shape();TopoDS_Shape side;
    for(const auto& f:ark::occt::shapes(cylinder,TopAbs_FACE))if(BRepAdaptor_Surface(TopoDS::Face(f)).GetType()==GeomAbs_Cylinder)side=f;
    check(!side.IsNull(),"Periodic trim regression side missing");
    auto trial=[&](const char* name,const TopoDS_Shape& source,const gp_Pnt& origin,const gp_Dir& normal,const char* keep,double expectedArea){
        V request=V::makeObj(),xyz=V::makeArr(),axis=V::makeArr(),row=V::makeObj();request.set("op",V::makeStr("surface_trim_plane"));request.set("input_brep",V::makeStr(ark::occt::brepData(source)));xyz.arr={V::makeNum(origin.X()),V::makeNum(origin.Y()),V::makeNum(origin.Z())};axis.arr={V::makeNum(normal.X()),V::makeNum(normal.Y()),V::makeNum(normal.Z())};request.set("origin",xyz);request.set("normal",axis);request.set("keep",V::makeStr(keep));row.set("case",V::makeStr(name));
        bool rejected=false;V result;try{result=ark::occt::modelSurfaceNative(request);}catch(const std::exception& e){rejected=true;row.set("error",V::makeStr(e.what()));}
        check(rejected==(expectedArea<0),std::string("Unexpected exact plane trim regression outcome: ")+name);
        if(!rejected){const auto shape=ark::occt::readBrep(need(result,"brep_base64").str);check(BRepCheck_Analyzer(shape).IsValid()&&ark::occt::shapes(shape,TopAbs_FACE).size()==1,"Trim regression did not retain one valid exact face");const double area=ark::occt::properties(shape,'a');assertNear(area,expectedArea,1e-6,std::string("Exact plane trim area: ")+name);const auto& report=need(result,"report");check(need(report,"split_method").str=="exact_face_plane_splitter"&&need(report,"side_classification").str=="exact_brep_bounds_in_plane_frame","Trim did not expose exact split/classification method");assertNear(need(report,"positive_area_mm2").num+need(report,"negative_area_mm2").num,ark::occt::properties(source,'a'),1e-6,"Both trim sides conserve area");check(need(report,"classification_tolerance_mm").num==1e-6,"Plane trim widened classification tolerance");row.set("area_mm2",V::makeNum(area));row.set("report",report);}
        row.set("expected",V::makeStr(expectedArea<0?"reject":"accept"));row.set("passed",V::makeBool(true));rows.arr.push_back(row);
    };
    const double area=ark::occt::properties(side,'a');
    trial("periodic-positive",side,{0,0,6},{0,0,1},"positive",area*.7);
    trial("periodic-negative",side,{0,0,6},{0,0,1},"negative",area*.3);
    trial("periodic-inverted-normal",side,{0,0,6},{0,0,-1},"positive",area*.3);
    trial("periodic-oblique",side,{0,0,10},{1,0,2},"positive",area*.5);
    trial("periodic-boundary",side,{0,0,0},{0,0,1},"positive",-1);
    trial("periodic-outside",side,{0,0,25},{0,0,1},"positive",-1);
    trial("periodic-tangent",side,{8,0,0},{1,0,0},"positive",-1);
    BRepBuilderAPI_MakePolygon outline;for(const gp_Pnt& p:{gp_Pnt(0,0,0),gp_Pnt(10,0,0),gp_Pnt(10,2,0),gp_Pnt(2,2,0),gp_Pnt(2,8,0),gp_Pnt(10,8,0),gp_Pnt(10,10,0),gp_Pnt(0,10,0)})outline.Add(p);outline.Close();BRepBuilderAPI_MakeFace cFace(outline.Wire());check(cFace.IsDone(),"Disconnected trim regression fixture failed");
    trial("coplanar-ambiguous",cFace.Face(),{0,0,0},{0,0,1},"positive",-1);
    trial("disconnected-side",cFace.Face(),{5,0,0},{1,0,0},"positive",-1);
    return rows;
}
}

int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--probe-periodic-trim")try{return periodicTrimProbe();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    V receipt=V::makeObj(),results=V::makeArr();int failures=0,accepted=0,rejected=0;
    try{
        check(argc==2||argc==4,"Expected manifest path [--receipt output.json]");const fs::path manifestPath(reinterpret_cast<const char8_t*>(argv[1]));const auto manifestBytes=bytes(manifestPath);const auto manifest=parse(manifestBytes);check(need(manifest,"schema").str=="ark.regression.corpus.v1","Unexpected native corpus manifest schema");receipt.set("schema",V::makeStr("ark.native.authoring.corpus.receipt.v1"));receipt.set("manifest_sha256",V::makeStr(ark::iterationSha256(manifestBytes)));receipt.set("seed",need(manifest,"seed"));receipt.set("geometry_tolerance_mm",V::makeNum(1e-6));receipt.set("authority",V::makeStr("native exact B-rep; no mesh fallback or automatic gap healing"));
        receipt.set("iteration_code",V::makeStr(ARK_ITERATION_CODE));receipt.set("version",V::makeStr(ARK_APP_VERSION));receipt.set("kernel_version",V::makeStr(ark::occt::kernelVersion()));const auto executable=fs::absolute(fs::path(reinterpret_cast<const char8_t*>(argv[0])));if(fs::is_regular_file(executable))receipt.set("test_binary_sha256",V::makeStr(ark::iterationSha256(bytes(executable))));
        receipt.set("plane_trim_regressions",planeTrimRegressions());
        const auto& fixtures=need(manifest,"native_fixtures");check(fixtures.t==V::Arr&&!fixtures.arr.empty(),"Native corpus fixture inventory missing");
        for(const auto& fixture:fixtures.arr){const auto name=need(fixture,"path").str;check(name.find("..") == std::string::npos&&fs::path(name).is_relative(),"Native fixture must remain within corpus");const auto source=bytes(manifestPath.parent_path()/name);check(source.find('\r')==std::string::npos,"Native corpus input must use canonical LF bytes");check(ark::iterationSha256(source)==need(fixture,"sha256").str,"Native corpus fixture hash differs");const auto corpus=parse(source);check(need(corpus,"schema").str=="ark.native.authoring.corpus.v1"&&need(corpus,"seed").num==need(manifest,"seed").num,"Native corpus identity mismatch");check(need(corpus,"fixed_geometry_tolerance_mm").num==1e-6,"Native corpus cannot widen geometry tolerance");receipt.set("fixture_sha256",V::makeStr(ark::iterationSha256(source)));receipt.set("limits",need(corpus,"scope"));
            for(const auto& example:need(corpus,"cases").arr)for(const auto& variant:need(corpus,"variants").arr){ark::AgentSession session;session.suppressFailureCapture();std::vector<V> trials;V baseline=V::makeObj(),commands=V::makeArr();for(const auto& p:variant.obj){V c=V::makeObj();c.set("op",V::makeStr("param.set"));c.set("name",V::makeStr(p.first));c.set("expr",p.second);commands.arr.push_back(c);}for(const auto& p:need(example,"parameters").obj){V c=V::makeObj();c.set("op",V::makeStr("param.set"));c.set("name",V::makeStr(p.first));c.set("expr",p.second);commands.arr.push_back(c);}for(const auto& command:need(example,"setup").arr)commands.arr.push_back(command);baseline.set("id",V::makeStr("baseline"));baseline.set("expected",V::makeStr("accept"));baseline.set("commands",commands);baseline.set("checks",need(example,"setup_checks"));trials.push_back(baseline);for(const auto& trial:need(example,"trials").arr)trials.push_back(trial);
                for(const auto& trial:trials){V row=V::makeObj();row.set("case",need(example,"id"));row.set("family",need(example,"family"));row.set("variant",variant);row.set("trial",need(trial,"id"));row.set("expected",need(trial,"expected"));const auto before=ark::documentToJson(session.document),geometry=evaluatedDigest(session.document);const auto revision=session.revision;row.set("before_authoring_sha256",V::makeStr(ark::iterationSha256(before)));bool responseAccepted=false;
                    try{V args=V::makeObj();args.set("commands",need(trial,"commands"));args.set("expected_revision",V::makeNum((double)revision));const auto reply=session.call("doc_apply",args);const auto& data=need(reply,"structuredContent");responseAccepted=!need(reply,"isError").b;row.set("observed",V::makeStr(responseAccepted?"accept":"reject"));
                        if(!responseAccepted){row.set("error_code",need(data,"error_code"));row.set("error",need(data,"error"));}
                        check(responseAccepted==(need(trial,"expected").str=="accept"),"Unexpected native corpus transaction outcome: "+data.dump());
                        if(responseAccepted){check(session.revision==revision+1,"Accepted native trial did not advance exactly one revision");row.set("exact_bodies",exactInventory(session.document));row.set("measurements",measuredChecks(session.document,need(trial,"checks")));persistence(session.document);row.set("native_readback_recomputed",V::makeBool(true));++accepted;}
                        else{const auto& expected=need(trial,"expected_codes");check(std::any_of(expected.arr.begin(),expected.arr.end(),[&](const V& v){return v.str==need(data,"error_code").str;}),"Native rejection lacks its expected typed error");if(const auto* fragment=trial.find("error_contains"))check(need(data,"error").str.find(fragment->str)!=std::string::npos,"Native rejection did not identify its explicit failure cause");check(session.revision==revision&&ark::documentToJson(session.document)==before&&evaluatedDigest(session.document)==geometry,"Rejected native trial changed source, journal, revision or exact geometry");row.set("document_unchanged",V::makeBool(true));++rejected;}
                        row.set("passed",V::makeBool(true));
                    }catch(const std::exception& error){++failures;row.set("passed",V::makeBool(false));row.set("failure",V::makeStr(error.what()));std::cerr<<need(example,"id").str<<" scale="<<variant.dump()<<" "<<need(trial,"id").str<<": "<<error.what()<<'\n';
                        if(responseAccepted&&need(trial,"expected").str=="reject"){std::string error;ark::Document restored;check(ark::documentFromJson(before,restored,&error),"Cannot restore failed trial baseline");restored.recompute();session.document=std::move(restored);session.revision=revision;}
                    }
                    row.set("after_authoring_sha256",V::makeStr(ark::iterationSha256(ark::documentToJson(session.document))));row.set("revision",V::makeNum((double)session.revision));results.arr.push_back(row);
                    if(need(trial,"id").str=="baseline"&&!need(row,"passed").b)break;
                }
            }
        }
    }catch(const std::exception& error){++failures;receipt.set("fatal_error",V::makeStr(error.what()));std::cerr<<error.what()<<'\n';}
    receipt.set("accepted_trials",V::makeNum(accepted));receipt.set("rejected_trials",V::makeNum(rejected));receipt.set("failures",V::makeNum(failures));receipt.set("trials",results);
    if(argc==4){try{check(std::string(argv[2])=="--receipt","Unknown native corpus output option");const fs::path destination(reinterpret_cast<const char8_t*>(argv[3]));std::ofstream out(destination,std::ios::binary);const auto data=receipt.dump()+"\n";out.write(data.data(),(std::streamsize)data.size());out.flush();check((bool)out,"Cannot write native corpus receipt");}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}}
    std::cout<<"B.03 native authoring corpus: "<<results.arr.size()<<" trials, "<<accepted<<" accepted, "<<rejected<<" rejected with preserved source, "<<failures<<" failures; seed=19020\n";return failures?1:0;
}
