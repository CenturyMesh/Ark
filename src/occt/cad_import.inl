// Included once at global scope by the isolated OCCT worker. Keep exact
// exchange transfer separate from the specialized planar tube importer.
#include <STEPControl_Reader.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESData_IGESModel.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <NCollection_Sequence.hxx>
#include <TCollection_AsciiString.hxx>
#include <gp_Ax2.hxx>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <limits>

namespace ark::occt {
namespace cadimport {
const V& required(const V& value,const char* key){auto p=value.find(key);if(!p)throw std::runtime_error(std::string("Missing CAD field: ")+key);return *p;}
std::string text(const V& value,const char* key){const auto& p=required(value,key);if(p.t!=V::Str||p.str.empty())throw std::runtime_error(std::string("Invalid CAD text: ")+key);return p.str;}
std::string utf8(const fs::path& path){auto value=path.u8string();return std::string(value.begin(),value.end());}
std::string format(const fs::path& path){std::string extension=utf8(path.extension());for(auto& c:extension)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));if(extension==".step"||extension==".stp")return "step";if(extension==".iges"||extension==".igs")return "iges";throw std::runtime_error("Exact CAD import supports STEP (.step/.stp) and IGES (.iges/.igs)");}
struct Snapshot {
    fs::path directory,path;
    Snapshot(const std::string& bytes,const std::string& sourceFormat){
        static std::atomic<unsigned long long> serial{0};
        directory=fs::temp_directory_path()/("ark-cad-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
        if(!fs::create_directory(directory))throw std::runtime_error("Cannot create private CAD source snapshot");
        path=directory/(sourceFormat=="step"?"source.step":"source.igs");
        try{writeFile(path,bytes);}catch(...){std::error_code error;fs::remove(path,error);fs::remove(directory,error);throw;}
    }
    ~Snapshot(){std::error_code error;fs::remove(path,error);fs::remove(directory,error);}
};
struct Transfer {TopoDS_Shape shape;V units=V::makeObj();int roots=0;};
template<class Reader>void allRoots(Reader& reader,Transfer& result){
    result.roots=reader.NbRootsForTransfer();
    if(result.roots<1||result.roots>10000)throw std::runtime_error("CAD source requires 1..10000 transferable geometry roots");
    for(int index=1;index<=result.roots;++index)if(!reader.TransferOneRoot(index))throw std::runtime_error("CAD geometry root "+std::to_string(index)+" failed transfer; no partial source is imported");
    result.shape=reader.OneShape();
    if(result.shape.IsNull()||!BRepCheck_Analyzer(result.shape).IsValid())throw std::runtime_error("Imported CAD failed exact B-rep validation");
}
Transfer read(const fs::path& path,const std::string& sourceFormat){
    Transfer result;const auto name=utf8(path);result.units.set("model_length_unit",V::makeStr("mm"));result.units.set("system_length_unit_mm",V::makeNum(1));
    if(sourceFormat=="step"){
        STEPControl_Reader reader;
        if(reader.ReadFile(name.c_str())!=IFSelect_RetDone)throw std::runtime_error("STEP source read failed");
        reader.SetSystemLengthUnit(1.0);NCollection_Sequence<TCollection_AsciiString> length,angle,solidAngle;reader.FileUnits(length,angle,solidAngle);
        V declared=V::makeArr();for(int i=1;i<=length.Length();++i)declared.arr.push_back(V::makeStr(length.Value(i).ToCString()));result.units.set("declared_length_units",declared);
        allRoots(reader,result);
    }else if(sourceFormat=="iges"){
        IGESControl_Controller::Init();IGESControl_Reader reader;reader.SetReadVisible(false);
        if(reader.ReadFile(name.c_str())!=IFSelect_RetDone)throw std::runtime_error("IGES source read failed");
        auto model=reader.IGESModel();if(model.IsNull())throw std::runtime_error("IGES has no source model");
        auto& global=model->ChangeGlobalSection();global.SetCascadeUnit(1.0);
        const double factor=global.UnitValue();if(!std::isfinite(factor)||factor<=0||global.UnitFlag()==3)throw std::runtime_error("IGES length unit is unsupported; custom units require an explicit conversion contract");
        result.units.set("declared_unit_flag",V::makeNum(global.UnitFlag()));result.units.set("source_length_to_mm",V::makeNum(factor));
        if(!global.UnitName().IsNull())result.units.set("declared_length_unit",V::makeStr(global.UnitName()->ToCString()));
        result.units.set("declared_model_scale",V::makeNum(global.Scale()));
        result.units.set("invisible_geometry_included",V::makeBool(true));allRoots(reader,result);
    }else throw std::runtime_error("Unsupported embedded CAD source format");
    return result;
}
V census(const TopoDS_Shape& shape){
    for(double value:bounds(shape))if(!std::isfinite(value)||std::abs(value)>1e8)throw std::runtime_error("CAD source bounds must be finite within 1e8 mm");
    const double area=properties(shape,'a');if(!std::isfinite(area)||area<=0)throw std::runtime_error("CAD source surface area must be finite and positive");
    V result=V::makeObj();for(const auto& item:std::vector<std::pair<const char*,TopAbs_ShapeEnum>>{{"solids",TopAbs_SOLID},{"shells",TopAbs_SHELL},{"faces",TopAbs_FACE},{"wires",TopAbs_WIRE},{"edges",TopAbs_EDGE},{"vertices",TopAbs_VERTEX}})result.set(item.first,V::makeNum(shapes(shape,item.second).size()));
    const auto faces=shapes(shape,TopAbs_FACE),solids=shapes(shape,TopAbs_SOLID);if(faces.empty())throw std::runtime_error("CAD source contains no faces; curve/point-only exchange is outside this import domain");
    if(faces.size()>16000||solids.size()>10000)throw std::runtime_error("CAD exact topology exceeds the supported import inventory");
    TopTools_IndexedMapOfShape solidFaces;for(const auto& solid:solids){shapeReport(solid,true);TopExp::MapShapes(solid,TopAbs_FACE,solidFaces);}
    size_t surfaceFaces=0;for(const auto& face:faces)if(!solidFaces.Contains(face))++surfaceFaces;
    TopTools_IndexedMapOfShape faceEdges,edgeVertices;for(const auto& face:faces)TopExp::MapShapes(face,TopAbs_EDGE,faceEdges);
    size_t freeEdges=0,freeVertices=0;for(const auto& edge:shapes(shape,TopAbs_EDGE)){if(!faceEdges.Contains(edge))++freeEdges;TopExp::MapShapes(edge,TopAbs_VERTEX,edgeVertices);}for(const auto& vertex:shapes(shape,TopAbs_VERTEX))if(!edgeVertices.Contains(vertex))++freeVertices;
    result.set("surface_only_faces",V::makeNum(surfaceFaces));result.set("free_edges",V::makeNum(freeEdges));result.set("free_vertices",V::makeNum(freeVertices));result.set("whole_source_retained",V::makeBool(true));result.set("body_selection",V::makeStr("all_transferable_geometry_roots"));return result;
}
V faces(const TopoDS_Shape& shape){
    V rows=V::makeArr();const char* names[]={"plane","cylinder","cone","sphere","torus","bezier","bspline","revolution","extrusion","offset","other"};size_t index=0;
    for(const auto& item:shapes(shape,TopAbs_FACE)){
        auto face=TopoDS::Face(item);BRepAdaptor_Surface surface(face);V row=V::makeObj(),uv=V::makeArr();const int kind=static_cast<int>(surface.GetType());
        row.set("face_index",V::makeNum(index++));row.set("type",V::makeStr(names[kind>=0&&kind<10?kind:10]));row.set("area_mm2",V::makeNum(properties(face,'a')));
        for(double value:{surface.FirstUParameter(),surface.LastUParameter(),surface.FirstVParameter(),surface.LastVParameter()}){if(!std::isfinite(value)||std::abs(value)>=1e100)throw std::runtime_error("Imported CAD has an unbounded face domain");uv.arr.push_back(V::makeNum(value));}row.set("trimmed_uv_domain",uv);
        if(surface.GetType()==GeomAbs_Plane){row.set("origin_mm",vec(surface.Plane().Location().XYZ()));auto normal=surface.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();row.set("normal",vec(normal.XYZ()));}
        if(surface.GetType()==GeomAbs_Cylinder){auto cylinder=surface.Cylinder();row.set("axis_origin_mm",vec(cylinder.Location().XYZ()));row.set("axis",vec(cylinder.Axis().Direction().XYZ()));row.set("radius_mm",V::makeNum(cylinder.Radius()));row.set("feature_intent_inferred",V::makeBool(false));}
        rows.arr.push_back(row);
    }return rows;
}
V equivalence(const TopoDS_Shape& source,const TopoDS_Shape& candidate){
    V evidence=V::makeObj();const auto a=bounds(source),b=bounds(candidate);double error=0;for(size_t i=0;i<6;++i)error=std::max(error,std::abs(a[i]-b[i]));
    auto differenceVolume=[](const TopoDS_Shape& a,const TopoDS_Shape& b){TopTools_ListOfShape arguments,tools;arguments.Append(a);tools.Append(b);BRepAlgoAPI_Cut algorithm;algorithm.SetArguments(arguments);algorithm.SetTools(tools);algorithm.SetNonDestructive(true);algorithm.Build();if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("Exact CAD equivalence Boolean failed");return std::abs(properties(algorithm.Shape()));};
    const double difference=differenceVolume(source,candidate)+differenceVolume(candidate,source),areaError=std::abs(properties(source,'a')-properties(candidate,'a'));
    const double volumeTolerance=std::max(1e-6,std::abs(properties(source))*1e-8),areaTolerance=std::max(1e-6,properties(source,'a')*1e-8);
    evidence.set("symmetric_difference_mm3",V::makeNum(difference));evidence.set("bounds_error_mm",V::makeNum(error));evidence.set("area_error_mm2",V::makeNum(areaError));evidence.set("volume_tolerance_mm3",V::makeNum(volumeTolerance));evidence.set("length_tolerance_mm",V::makeNum(1e-6));evidence.set("area_tolerance_mm2",V::makeNum(areaTolerance));
    evidence.set("equivalent_in_scope",V::makeBool(difference<=volumeTolerance&&error<=1e-6&&areaError<=areaTolerance));return evidence;
}
V recognize(const TopoDS_Shape& shape){
    V result=V::makeObj(),bodies=V::makeArr();size_t index=0,recognized=0;
    for(const auto& solid:shapes(shape,TopAbs_SOLID)){
        V row=V::makeObj(),recipe=V::makeObj(),parameters=V::makeObj();row.set("body_index",V::makeNum(index++));row.set("status",V::makeStr("unresolved"));
        const auto inventory=shapes(solid,TopAbs_FACE);const auto box=bounds(solid);TopoDS_Shape candidate;gp_Trsf placement;std::string kind;
        if(inventory.size()==6&&std::all_of(inventory.begin(),inventory.end(),[](const auto& face){return BRepAdaptor_Surface(TopoDS::Face(face)).GetType()==GeomAbs_Plane;})){
            const double width=box[3]-box[0],depth=box[4]-box[1],height=box[5]-box[2];
            if(width>1e-8&&depth>1e-8&&height>1e-8){placement.SetTranslation(gp_Vec(box[0],box[1],box[2]));candidate=transformed(BRepPrimAPI_MakeBox(width,depth,height).Shape(),placement);kind="box";parameters.set("width",V::makeNum(width));parameters.set("depth",V::makeNum(depth));parameters.set("height",V::makeNum(height));}
        }else if(inventory.size()==3){
            TopoDS_Face side;std::vector<gp_Pnt> caps;bool supported=true;
            for(const auto& item:inventory){BRepAdaptor_Surface surface(TopoDS::Face(item));if(surface.GetType()==GeomAbs_Cylinder&&side.IsNull())side=TopoDS::Face(item);else if(surface.GetType()==GeomAbs_Plane)caps.push_back(surface.Plane().Location());else supported=false;}
            if(supported&&!side.IsNull()&&caps.size()==2){auto cylinder=BRepAdaptor_Surface(side).Cylinder();auto axis=cylinder.Axis().Direction();const auto origin=cylinder.Location();double a=gp_Vec(origin,caps[0]).Dot(gp_Vec(axis)),b=gp_Vec(origin,caps[1]).Dot(gp_Vec(axis));if(a>b)std::swap(a,b);
                if(b-a>1e-8){auto start=origin.Translated(gp_Vec(axis)*a);gp_Ax2 frame(start,axis,cylinder.XAxis().Direction());candidate=BRepPrimAPI_MakeCylinder(frame,cylinder.Radius(),b-a).Shape();kind="cylinder";parameters.set("radius",V::makeNum(cylinder.Radius()));parameters.set("height",V::makeNum(b-a));const auto x=frame.XDirection(),y=frame.YDirection(),z=frame.Direction();placement.SetValues(x.X(),y.X(),z.X(),start.X(),x.Y(),y.Y(),z.Y(),start.Y(),x.Z(),y.Z(),z.Z(),start.Z());}
            }
        }
        if(!candidate.IsNull()){
            try{V evidence=equivalence(solid,candidate);row.set("equivalence",evidence);if(required(evidence,"equivalent_in_scope").b){row.set("status",V::makeStr("measured_primitive_candidate"));recipe.set("op",V::makeStr(kind));recipe.set("parameters",parameters);recipe.set("placement",matrix(placement));row.set("recipe",recipe);++recognized;}}
            catch(const std::exception& error){row.set("recognition_error",V::makeStr(error.what()));}
        }
        if(!row.find("recipe"))row.set("reason",V::makeStr("No supported exact primitive reconstruction proved; inspect and rebuild through native authoring or preserve as exact reference"));bodies.arr.push_back(row);
    }
    result.set("bodies",bodies);result.set("proven_primitive_candidates",V::makeNum(recognized));result.set("automatic_conversion_complete",V::makeBool(false));result.set("original_history_recovered",V::makeBool(false));result.set("manufacturing_qualified",V::makeBool(false));result.set("scope",V::makeStr("axis-aligned boxes and single analytic cylinders; holes and arbitrary surfaces require agent reconstruction and equivalence proof"));return result;
}
V inspection(const Transfer& transferred,const std::string& sourceFormat,const std::string& sourceName,const std::string& sourceHash){
    V result=V::makeObj(),report=shapeReport(transferred.shape,false);result.set("source_format",V::makeStr(sourceFormat));result.set("source_name",V::makeStr(sourceName));result.set("source_sha256",V::makeStr(sourceHash));result.set("census",census(transferred.shape));result.set("units",transferred.units);
    report.set("geometry_authority",V::makeStr("exact_source_brep"));report.set("transferred_roots",V::makeNum(transferred.roots));report.set("partial_transfer_allowed",V::makeBool(false));report.set("face_inventory",faces(transferred.shape));result.set("report",report);result.set("recognition",recognize(transferred.shape));return result;
}
V fromFile(const V& request,bool packageRequired){
    const fs::path path=fs::absolute(fs::u8path(text(request,"path")));const std::string sourceFormat=format(path);
    if(!fs::is_regular_file(path)||fs::file_size(path)==0||fs::file_size(path)>64u*1024u*1024u)throw std::runtime_error("CAD import requires a nonempty regular file within 64 MiB");
    const auto bytes=readFile(path);if(bytes.empty()||bytes.size()>64u*1024u*1024u)throw std::runtime_error("CAD source snapshot exceeds the supported file bound");Snapshot snapshot(bytes,sourceFormat);const auto transferred=read(snapshot.path,sourceFormat);const auto hash=sha256(bytes);
    // Freeze the exact transfer before inspection/recognition algorithms can
    // populate or alter auxiliary representation data on shared OCCT shapes.
    const auto encoded=packageRequired?brepData(transferred.shape):std::string();auto result=inspection(transferred,sourceFormat,utf8(path.filename()),hash);
    if(packageRequired){V package=V::makeObj();package.set("format",V::makeStr("ark-source-cad"));package.set("version",V::makeNum(1));package.set("source_format",V::makeStr(sourceFormat));package.set("source_name",V::makeStr(utf8(path.filename())));package.set("source_sha256",V::makeStr(hash));package.set("source_base64",V::makeStr(base64Encode(bytes)));package.set("brep_base64",V::makeStr(encoded));package.set("brep_sha256",V::makeStr(sha256(base64Decode(encoded))));package.set("census",required(result,"census"));package.set("units",transferred.units);result.set("package",package);}
    return result;
}
}
V inspectCadNative(const V& request){return cadimport::fromFile(request,false);}
V importCadNative(const V& request){return cadimport::fromFile(request,true);}
V cadCustodyNative(const V& package){
    using namespace cadimport;
    if(package.t!=V::Obj||text(package,"format")!="ark-source-cad"||required(package,"version").t!=V::Num||required(package,"version").num!=1)throw std::runtime_error("Unsupported exact CAD source package");
    const auto sourceFormat=text(package,"source_format");if(sourceFormat!="step"&&sourceFormat!="iges")throw std::runtime_error("Unsupported embedded CAD format");
    const auto source=base64Decode(text(package,"source_base64")),brep=base64Decode(text(package,"brep_base64"));
    if(source.empty()||source.size()>64u*1024u*1024u||sha256(source)!=text(package,"source_sha256"))throw std::runtime_error("Embedded CAD source SHA-256 mismatch");
    if(brep.empty()||brep.size()>64u*1024u*1024u||sha256(brep)!=text(package,"brep_sha256"))throw std::runtime_error("Embedded exact CAD B-rep SHA-256 mismatch");
    V result=V::makeObj();result.set("source_sha256",required(package,"source_sha256"));result.set("brep_sha256",required(package,"brep_sha256"));result.set("source_bytes_checked",V::makeBool(true));result.set("source_file_required",V::makeBool(false));return result;
}
V modelImportedCadNative(const V& request){
    using namespace cadimport;const V& package=required(request,"package");cadCustodyNative(package);
    const auto bytes=base64Decode(text(package,"source_base64")),sourceFormat=text(package,"source_format");Snapshot snapshot(bytes,sourceFormat);const auto transferred=read(snapshot.path,sourceFormat);
    // Reproduce the exchange transfer from the immutable source. Matching the
    // exact serialized B-rep proves all geometry, including trimmed surfaces;
    // scalar bounds/area checks cannot prove that a surface was preserved.
    const auto sourceEncoded=brepData(transferred.shape);if(sha256(base64Decode(sourceEncoded))!=text(package,"brep_sha256"))throw std::runtime_error("Stored CAD B-rep is not the exact transfer of its embedded source");
    const auto stored=readBrep(text(package,"brep_base64"));auto inventory=census(stored);const auto& declared=required(package,"census");bool censusMatches=declared.t==V::Obj&&declared.obj.size()==inventory.obj.size();
    // Persistence canonicalizes JSON key order; compare the typed field values
    // rather than treating object serialization order as geometry evidence.
    for(const auto& item:inventory.obj){const auto* actual=declared.find(item.first.c_str());if(!actual||actual->dump()!=item.second.dump())censusMatches=false;}
    if(!censusMatches)throw std::runtime_error("Embedded CAD body/topology census mismatch");
    auto result=shapeResult(stored,false);auto report=required(result,"report");report.set("geometry_authority",V::makeStr("exact_source_brep"));report.set("source_sha256",required(package,"source_sha256"));report.set("source_format",required(package,"source_format"));report.set("source_transfer_reproduced",V::makeBool(true));report.set("whole_source_retained",V::makeBool(true));report.set("surface_only_faces",required(inventory,"surface_only_faces"));report.set("source_name",required(package,"source_name"));report.set("units",transferred.units);report.set("transferred_roots",V::makeNum(transferred.roots));report.set("original_history_recovered",V::makeBool(false));result.set("report",report);result.set("package",package);return result;
}
V inspectImportedCadNative(const V& request){
    using namespace cadimport;const V& package=required(request,"package");cadCustodyNative(package);
    const auto bytes=base64Decode(text(package,"source_base64")),sourceFormat=text(package,"source_format");Snapshot snapshot(bytes,sourceFormat);const auto transferred=read(snapshot.path,sourceFormat);
    if(sha256(base64Decode(brepData(transferred.shape)))!=text(package,"brep_sha256"))throw std::runtime_error("Stored CAD B-rep is not the exact transfer of its embedded source");
    auto result=inspection(transferred,sourceFormat,text(package,"source_name"),text(package,"source_sha256"));
    if(const auto* encoded=request.find("candidate_brep")){
        if(encoded->t!=V::Str||encoded->str.empty())throw std::runtime_error("CAD equivalence candidate requires exact B-rep");
        const auto candidate=readBrep(encoded->str);const auto sourceCensus=required(result,"census"),candidateCensus=census(candidate);V comparison=V::makeObj();
        if(required(sourceCensus,"surface_only_faces").num!=0||required(candidateCensus,"surface_only_faces").num!=0||required(sourceCensus,"free_edges").num!=0||required(candidateCensus,"free_edges").num!=0||required(sourceCensus,"free_vertices").num!=0||required(candidateCensus,"free_vertices").num!=0){comparison.set("equivalent_in_scope",V::makeBool(false));comparison.set("comparison_checked",V::makeBool(false));comparison.set("reason",V::makeStr("Free surface/trim, curve or point equivalence requires a separate exact proof; scalar properties cannot prove it"));}
        else if(required(sourceCensus,"solids").num!=required(candidateCensus,"solids").num){comparison.set("equivalent_in_scope",V::makeBool(false));comparison.set("comparison_checked",V::makeBool(true));comparison.set("reason",V::makeStr("Reconstructed candidate solid count differs from immutable source"));}
        else{comparison=equivalence(transferred.shape,candidate);comparison.set("comparison_checked",V::makeBool(true));comparison.set("method",V::makeStr("exact_brep_bidirectional_boolean_symmetric_difference_and_area_bounds"));}
        comparison.set("source_brep_sha256",required(package,"brep_sha256"));comparison.set("candidate_brep_sha256",V::makeStr(sha256(base64Decode(encoded->str))));comparison.set("source_census",sourceCensus);comparison.set("candidate_census",candidateCensus);comparison.set("manufacturing_qualified",V::makeBool(false));result.set("comparison",comparison);
    }return result;
}
}
