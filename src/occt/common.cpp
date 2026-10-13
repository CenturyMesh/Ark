#include "common.hpp"
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <OSD_ThreadPool.hxx>
#include <thread>
#include <cstdlib>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Standard_Version.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopLoc_Location.hxx>
#include <Poly_Triangulation.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAbs_CurveType.hxx>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ark::occt {
std::string readFile(const fs::path& path){
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot read native geometry file");
    file.seekg(0,std::ios::end);auto length=file.tellg();if(length<0||length>256*1024*1024)throw std::runtime_error("Native geometry file exceeds 256 MiB");file.seekg(0);
    return std::string(std::istreambuf_iterator<char>(file),{});
}
void writeFile(const fs::path& path,const std::string& bytes){
    if(!path.parent_path().empty())fs::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(bytes.data(),(std::streamsize)bytes.size());file.flush();if(!file)throw std::runtime_error("Cannot write native geometry file");
}
void replaceFile(const fs::path& staged,const fs::path& destination){
#ifdef _WIN32
    if(!MoveFileExW(staged.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish native file atomically");
#else
    fs::rename(staged,destination);
#endif
}
std::string sha256(const std::string& bytes){
    static constexpr uint32_t k[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto rr=[](uint32_t x,int n){return (x>>n)|(x<<(32-n));};
    std::vector<unsigned char> data(bytes.begin(),bytes.end());uint64_t bits=(uint64_t)data.size()*8;data.push_back(0x80);while(data.size()%64!=56)data.push_back(0);for(int i=7;i>=0;--i)data.push_back((unsigned char)(bits>>(i*8)));
    for(size_t offset=0;offset<data.size();offset+=64){uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(data[offset+i*4])<<24)|(uint32_t(data[offset+i*4+1])<<16)|(uint32_t(data[offset+i*4+2])<<8)|data[offset+i*4+3];
        for(int i=16;i<64;++i){uint32_t a=w[i-15],b=w[i-2];w[i]=w[i-16]+(rr(a,7)^rr(a,18)^(a>>3))+w[i-7]+(rr(b,17)^rr(b,19)^(b>>10));}
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],q=h[7];
        for(int i=0;i<64;++i){uint32_t t1=q+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^((~e)&g))+k[i]+w[i],t2=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&c)^(b&c));q=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=q;
    }
    const char* hex="0123456789abcdef";std::string out;for(auto word:h)for(int shift=28;shift>=0;shift-=4)out+=hex[(word>>shift)&15];return out;
}
std::string base64Encode(const std::string& bytes){
    const char* table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;out.reserve((bytes.size()+2)/3*4);
    for(size_t i=0;i<bytes.size();i+=3){uint32_t n=uint32_t((unsigned char)bytes[i])<<16;if(i+1<bytes.size())n|=uint32_t((unsigned char)bytes[i+1])<<8;if(i+2<bytes.size())n|=(unsigned char)bytes[i+2];out+=table[(n>>18)&63];out+=table[(n>>12)&63];out+=i+1<bytes.size()?table[(n>>6)&63]:'=';out+=i+2<bytes.size()?table[n&63]:'=';}return out;
}
std::string base64Decode(const std::string& text){
    if(text.size()%4||text.size()>256*1024*1024)throw std::runtime_error("Invalid base64 geometry size");
    auto digit=[](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;};
    std::string out;out.reserve(text.size()/4*3);
    for(size_t i=0;i<text.size();i+=4){int a=digit(text[i]),b=digit(text[i+1]),c=text[i+2]=='='?0:digit(text[i+2]),d=text[i+3]=='='?0:digit(text[i+3]);bool p=text[i+2]=='=',q=text[i+3]=='=';
        if(a<0||b<0||c<0||d<0||(p&&!q)||((p||q)&&i+4!=text.size())||(p&&(b&15))||(!p&&q&&(c&3)))throw std::runtime_error("Invalid base64 geometry encoding");
        uint32_t n=(a<<18)|(b<<12)|(c<<6)|d;out+=char(n>>16);if(!p)out+=char(n>>8);if(!q)out+=char(n);
    }return out;
}
TopoDS_Shape readStep(const fs::path& path){
    STEPControl_Reader reader;auto u8=path.u8string();std::string name(u8.begin(),u8.end());
    if(reader.ReadFile(name.c_str())!=IFSelect_RetDone)throw std::runtime_error("STEP read failed");reader.SetSystemLengthUnit(1.0);
    if(!reader.TransferRoots())throw std::runtime_error("STEP transfer failed");auto shape=reader.OneShape();
    if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())throw std::runtime_error("Invalid STEP B-rep");return shape;
}
void writeStep(const TopoDS_Shape& shape,const fs::path& path){
    if(!path.parent_path().empty())fs::create_directories(path.parent_path());auto u8=path.u8string();std::string name(u8.begin(),u8.end());STEPControl_Writer writer;
    if(writer.Transfer(shape,STEPControl_AsIs)!=IFSelect_RetDone||writer.Write(name.c_str())!=IFSelect_RetDone)throw std::runtime_error("STEP write failed");
}
namespace {
enum class VolumeMethod { AnalyticGauss, PolynomialTrimGauss, RationalSurfaceKronrod };
VolumeMethod volumeMethod(const TopoDS_Shape& shape){
    bool rational=false,polynomialTrim=false,freeformSurface=false;
    for(const auto& item:shapes(shape,TopAbs_EDGE)){
        if(BRep_Tool::Degenerated(TopoDS::Edge(item)))continue;
        BRepAdaptor_Curve curve(TopoDS::Edge(item));
        if(curve.GetType()==GeomAbs_BSplineCurve||curve.GetType()==GeomAbs_BezierCurve){polynomialTrim=true;rational|=curve.IsRational();}
    }
    for(const auto& item:shapes(shape,TopAbs_FACE)){
        auto face=TopoDS::Face(item);BRepAdaptor_Surface surface(face);
        freeformSurface|=surface.GetType()==GeomAbs_BSplineSurface||surface.GetType()==GeomAbs_BezierSurface;
        for(const auto& edge:shapes(face,TopAbs_EDGE)){
            if(BRep_Tool::Degenerated(TopoDS::Edge(edge)))continue;
            BRepAdaptor_Curve2d curve(TopoDS::Edge(edge),face);
            if(curve.GetType()==GeomAbs_BSplineCurve||curve.GetType()==GeomAbs_BezierCurve){polynomialTrim=true;rational|=curve.IsRational();}
        }
    }
    if(rational||freeformSurface)return VolumeMethod::RationalSurfaceKronrod;
    return polynomialTrim?VolumeMethod::PolynomialTrimGauss:VolumeMethod::AnalyticGauss;
}
}
const char* volumeIntegrationMethod(const TopoDS_Shape& shape){
    switch(volumeMethod(shape)){case VolumeMethod::AnalyticGauss:return "analytic_gauss";case VolumeMethod::PolynomialTrimGauss:return "adaptive_gauss_polynomial_trim";default:return "gauss_kronrod_rational_or_freeform";}
}
void volumeProperties(const TopoDS_Shape& shape,GProp_GProps& props,bool moments){
    double error=0;
    switch(volumeMethod(shape)){
        // Exact analytic surfaces and trims retain the original bounded Gauss rule.
        // GK moment refinement around their zero terms can take seconds per tube.
        case VolumeMethod::AnalyticGauss:BRepGProp::VolumeProperties(shape,props,false,false,false);break;
        // Polynomial trimming of analytic surfaces needs adaptive integration,
        // but not the expensive rational/freeform moment algorithm.
        case VolumeMethod::PolynomialTrimGauss:error=BRepGProp::VolumeProperties(shape,props,1e-12,false,false);break;
        case VolumeMethod::RationalSurfaceKronrod:error=BRepGProp::VolumePropertiesGK(shape,props,1e-10,false,true,moments,moments,false);break;
    }
    if(error<0||!std::isfinite(error))throw std::runtime_error("Exact volume integration failed");
}

double properties(const TopoDS_Shape& shape,char kind){GProp_GProps props;if(shape.IsNull())return 0;if(kind=='a')BRepGProp::SurfaceProperties(shape,props);else if(kind=='l')BRepGProp::LinearProperties(shape,props);else volumeProperties(shape,props,false);return props.Mass();}
std::vector<TopoDS_Shape> shapes(const TopoDS_Shape& shape,TopAbs_ShapeEnum kind){std::vector<TopoDS_Shape> out;for(TopExp_Explorer ex(shape,kind);ex.More();ex.Next())out.push_back(ex.Current());return out;}
std::array<double,6> bounds(const TopoDS_Shape& shape){Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);if(box.IsVoid())throw std::runtime_error("Empty native shape bounds");std::array<double,6> b;box.Get(b[0],b[1],b[2],b[3],b[4],b[5]);return b;}
template<class Op>TopoDS_Shape booleanOp(const TopoDS_Shape& a,const TopoDS_Shape& b){Op op(a,b);op.Build();if(!op.IsDone()||op.HasErrors())throw std::runtime_error("Native Boolean operation failed");return op.Shape();}
TopoDS_Shape cut(const TopoDS_Shape& a,const TopoDS_Shape& b){return booleanOp<BRepAlgoAPI_Cut>(a,b);}
TopoDS_Shape common(const TopoDS_Shape& a,const TopoDS_Shape& b){return booleanOp<BRepAlgoAPI_Common>(a,b);}
TopoDS_Shape fuse(const TopoDS_Shape& a,const TopoDS_Shape& b){return booleanOp<BRepAlgoAPI_Fuse>(a,b);}
TopoDS_Shape compound(const std::vector<TopoDS_Shape>& parts){BRep_Builder b;TopoDS_Compound out;b.MakeCompound(out);for(auto& p:parts)b.Add(out,p);return out;}
TopoDS_Shape transformed(const TopoDS_Shape& s,const gp_Trsf& t){BRepBuilderAPI_Transform op(s,t,true);if(!op.IsDone())throw std::runtime_error("Native shape transform failed");return op.Shape();}
V vec(const gp_XYZ& p){V out=V::makeArr();for(double n:{p.X(),p.Y(),p.Z()})out.arr.push_back(V::makeNum(n));return out;}
V matrix(const gp_Trsf& t){V out=V::makeArr();for(int c=1;c<=4;++c)for(int r=1;r<=4;++r)out.arr.push_back(V::makeNum(r<=3?t.Value(r,c):(c==4?1:0)));return out;}
int nativeThreadBudget(){
    static const int budget=[](){
        int threads=std::max(1,std::min(8,(int)std::thread::hardware_concurrency()));
        if(const char* requested=std::getenv("ARK_NATIVE_THREADS")){
            char* end=nullptr;long value=std::strtol(requested,&end,10);
            if(end!=requested&&!*end&&value>=1&&value<=64)threads=(int)value;
        }
        const auto& pool=OSD_ThreadPool::DefaultPool(threads);
        threads=std::min(threads,pool->NbThreads());pool->SetNbDefaultThreadsToLaunch(threads);return threads;
    }();return budget;
}
namespace {
// Rendering metadata only. The full, ordered edge polylines remain authoritative
// for picking and semantic index bindings. Unknown regularity stays visible.
bool visibleNativeEdge(const TopoDS_Edge& edge,const NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher>& owners){
    if(BRep_Tool::Degenerated(edge))return false; // Existing empty preview edge.
    if(!owners.Contains(edge))return true; // Standalone curve/wire.
    const auto& faces=owners.FindFromKey(edge);
    if(faces.Extent()!=2)return true; // Free boundaries and non-manifold uses.
    const auto first=TopoDS::Face(faces.First());const auto second=TopoDS::Face(faces.Last());
    if(first.IsSame(second)){
        if(!BRep_Tool::IsClosed(edge,first))return true;
        // Two p-curves prove a parametric seam, but not necessarily smoothness
        // for a C0 freeform surface. Analytic periodic surfaces are smooth away
        // from their already-degenerate poles/apex; other seams need regularity.
        const auto type=BRepAdaptor_Surface(first).GetType();
        if(type==GeomAbs_Cylinder||type==GeomAbs_Cone||type==GeomAbs_Sphere||type==GeomAbs_Torus)return false;
    }
    return !BRep_Tool::HasContinuity(edge,first,second)||BRep_Tool::Continuity(edge,first,second)<GeomAbs_G1;
}
}
V edgeVisibility(const TopoDS_Shape& shape){
    V visible=V::makeArr();NCollection_IndexedDataMap<TopoDS_Shape,NCollection_List<TopoDS_Shape>,TopTools_ShapeMapHasher> owners;
    TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,owners);
    for(const auto& item:shapes(shape,TopAbs_EDGE))visible.arr.push_back(V::makeBool(visibleNativeEdge(TopoDS::Edge(item),owners)));
    return visible;
}
V meshData(const TopoDS_Shape& s,double deflection,double angle){
    BRepMesh_IncrementalMesh mesher(s,deflection,false,angle,nativeThreadBudget()>1);if(!mesher.IsDone())throw std::runtime_error("Native preview tessellation failed");V pos=V::makeArr(),idx=V::makeArr(),spans=V::makeArr(),edges=V::makeArr(),edgeVisible=edgeVisibility(s);
    for(auto& item:shapes(s,TopAbs_FACE)){auto face=TopoDS::Face(item);TopLoc_Location loc;auto tri=BRep_Tool::Triangulation(face,loc);if(tri.IsNull())throw std::runtime_error("Native face has no triangulation");size_t base=pos.arr.size()/3,start=idx.arr.size()/3;
        for(int i=1;i<=tri->NbNodes();++i){auto p=tri->Node(i).Transformed(loc.Transformation());for(auto v:{p.X(),p.Y(),p.Z()})pos.arr.push_back(V::makeNum(std::round(v*1e8)/1e8));}
        for(int i=1;i<=tri->NbTriangles();++i){int a,b,c;tri->Triangle(i).Get(a,b,c);if(face.Orientation()==TopAbs_REVERSED)std::swap(b,c);for(int v:{a,b,c})idx.arr.push_back(V::makeNum((double)(base+v-1)));}
        V span=V::makeArr();span.arr.push_back(V::makeNum((double)start));span.arr.push_back(V::makeNum((double)(idx.arr.size()/3-start)));spans.arr.push_back(std::move(span));
        if(pos.arr.size()>6000000||idx.arr.size()>6000000)throw std::runtime_error("Native preview exceeds mesh size limit");
    }
    for(auto& item:shapes(s,TopAbs_EDGE)){V poly=V::makeArr();const auto edge=TopoDS::Edge(item);
        if(BRep_Tool::Degenerated(edge)){edges.arr.push_back(std::move(poly));continue;}
        BRepAdaptor_Curve curve(edge);const double a=curve.FirstParameter(),b=curve.LastParameter();
        if(!std::isfinite(a)||!std::isfinite(b))throw std::runtime_error("Native edge preview has an unbounded parameter range");
        auto append=[&](const gp_Pnt& p){for(double v:{p.X(),p.Y(),p.Z()})poly.arr.push_back(V::makeNum(std::round(v*1e8)/1e8));};
        if(curve.GetType()==GeomAbs_Line||a==b){append(curve.Value(a));append(curve.Value(b));}
        else{
            // Display/picking follows curved B-rep edges at the same deflection
            // as the surface mesh, rather than a coarse fixed twelve chords.
            GCPnts_QuasiUniformDeflection sampler(curve,deflection,a,b,GeomAbs_C0);
            if(!sampler.IsDone()||sampler.NbPoints()<2||sampler.NbPoints()>16384)throw std::runtime_error("Native edge preview tessellation failed or exceeds its bound");
            // An inflected spline can put both endpoints AND its midpoint on
            // one chord. Check quarter points too and refine the display
            // polyline; these sampled checks are not an exact-curve certificate.
            auto chordDistance=[](const gp_Pnt& p,const gp_Pnt& start,const gp_Pnt& end){
                const gp_Vec chord(start,end),offset(start,p);const double length2=chord.SquareMagnitude();
                const double t=length2>1e-24?std::clamp(offset.Dot(chord)/length2,0.0,1.0):0.0;
                return p.Distance(start.Translated(chord*t));
            };
            size_t points=1;append(curve.Value(sampler.Parameter(1)));
            auto refine=[&](auto&& self,double u,double v,const gp_Pnt& start,const gp_Pnt& end,int depth)->void{
                const double mid=u+(v-u)*0.5;const auto center=curve.Value(mid);
                double error=chordDistance(center,start,end);
                for(double fraction:{0.25,0.75})error=std::max(error,chordDistance(curve.Value(u+(v-u)*fraction),start,end));
                if(!std::isfinite(error))throw std::runtime_error("Native edge preview has nonfinite samples");
                if(error>deflection){
                    if(depth>=24||mid==u||mid==v)throw std::runtime_error("Native edge preview refinement exceeds its bound");
                    self(self,u,mid,start,center,depth+1);self(self,mid,v,center,end,depth+1);
                }else{if(++points>16384)throw std::runtime_error("Native edge preview exceeds its point bound");append(end);}
            };
            for(int i=2;i<=sampler.NbPoints();++i){const double u=sampler.Parameter(i-1),v=sampler.Parameter(i);refine(refine,u,v,curve.Value(u),curve.Value(v),0);}
        }
        edges.arr.push_back(std::move(poly));
    }
    V mesh=V::makeObj();mesh.set("pos",pos);mesh.set("idx",idx);mesh.set("face_spans",spans);mesh.set("edges",edges);mesh.set("edge_visible",edgeVisible);return mesh;
}
std::string brepData(const TopoDS_Shape& shape){std::ostringstream stream;BRepTools::Write(shape,stream,false,false,TopTools_FormatVersion_CURRENT);return base64Encode(stream.str());}
TopoDS_Shape readBrep(const std::string& encoded){std::istringstream stream(base64Decode(encoded));TopoDS_Shape shape;BRep_Builder b;BRepTools::Read(shape,stream,b);if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())throw std::runtime_error("Invalid native B-rep payload");return shape;}
std::string kernelVersion(){return OCC_VERSION_COMPLETE;}
V shapeReport(const TopoDS_Shape& shape,bool requireSolid){
    if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())throw std::runtime_error("Native result failed B-rep validation");
    auto solids=shapes(shape,TopAbs_SOLID);
    const double measuredVolume=solids.empty()?0:properties(shape);
    if(requireSolid&&(solids.size()!=1||measuredVolume<=0)){
        std::ostringstream message;
        message<<"Native operation must produce one positive-volume solid; result contains "<<solids.size()<<" solids";
        if(!solids.empty())message<<" with total signed volume "<<measuredVolume<<" mm3";
        if(solids.size()>1)message<<". A union may contain disconnected parts or a cut may have split its base. Keep separate parts as assembly instances, or revise contact/cutter geometry; inspect inputs before retrying.";
        else if(solids.empty())message<<". The result is empty or has no solid; check overlap, cutter extent and closed profiles.";
        else message<<". Check orientation and degenerate dimensions.";
        throw std::runtime_error(message.str());
    }
    V report=V::makeObj();report.set("kernel",V::makeStr("OCCT"));report.set("kernel_version",V::makeStr(kernelVersion()));report.set("brep_valid",V::makeBool(true));report.set("solids",V::makeNum(solids.size()));report.set("faces",V::makeNum(shapes(shape,TopAbs_FACE).size()));report.set("area_mm2",V::makeNum(properties(shape,'a')));report.set("volume_mm3",solids.empty()?V{}:V::makeNum(measuredVolume));report.set("display_mesh_authoritative",V::makeBool(false));report.set("self_intersections_checked",V::makeBool(false));report.set("manufacturing_authoritative",V::makeBool(false));
    V b=V::makeArr();for(auto x:bounds(shape))b.arr.push_back(V::makeNum(x));report.set("bounds_mm",b);return report;
}
V completeSelectionReferences(V result){
    const auto* encoded=result.find("brep_base64");
    if(!encoded||encoded->t!=V::Str)return result;
    const std::string identity=sha256(base64Decode(encoded->str));
    const auto shape=readBrep(encoded->str);
    const auto faces=shapes(shape,TopAbs_FACE),edges=shapes(shape,TopAbs_EDGE);
    auto describe=[](const TopoDS_Shape& entity,TopAbs_ShapeEnum kind){V descriptor=V::makeObj();
        if(kind==TopAbs_FACE){BRepAdaptor_Surface surface(TopoDS::Face(entity));
            if(surface.GetType()==GeomAbs_Plane){const auto plane=surface.Plane();auto normal=plane.Axis().Direction();if(entity.Orientation()==TopAbs_REVERSED)normal.Reverse();descriptor.set("origin",vec(plane.Location().XYZ()));descriptor.set("normal",vec(normal.XYZ()));descriptor.set("x_axis",vec(plane.XAxis().Direction().XYZ()));}
            else descriptor.set("surface_type",V::makeStr("non_planar"));
        }else if(!BRep_Tool::Degenerated(TopoDS::Edge(entity))){BRepAdaptor_Curve curve(TopoDS::Edge(entity));const double u=curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*.5;gp_Pnt p;gp_Vec tangent;curve.D1(u,p,tangent);descriptor.set("origin",vec(p.XYZ()));if(tangent.Magnitude()>1e-12){tangent.Normalize();descriptor.set("tangent",vec(tangent.XYZ()));}}
        return descriptor;
    };
    V topology=result.find("topology")?*result.find("topology"):V::makeObj();
    if(topology.t!=V::Obj)throw std::runtime_error("Invalid native selection topology");
    if(const auto* digest=topology.find("brep_sha256");digest&&(digest->t!=V::Str||digest->str!=identity))throw std::runtime_error("Selection topology B-rep digest mismatch");
    topology.set("brep_sha256",V::makeStr(identity));
    V rows=topology.find("references")?*topology.find("references"):V::makeArr();
    if(rows.t!=V::Arr||rows.arr.size()>16384)throw std::runtime_error("Selection reference inventory exceeds16384 entries");
    std::set<std::string> names;size_t semanticCount=0;
    for(auto& row:rows.arr){
        const auto* name=row.find("name");if(!name||name->t!=V::Str||!names.insert(name->str).second)throw std::runtime_error("Invalid or duplicate selection reference");
        const auto marker=name->str.find("@ark.");
        if(marker!=std::string::npos){
            const std::string referenceName=name->str;
            const auto digestStart=name->str.find('.',marker+5);
            const auto originalDigest=digestStart==std::string::npos?std::string():name->str.substr(digestStart+1,64);
            const auto kindName=digestStart==std::string::npos?std::string():referenceName.substr(marker+5,digestStart-marker-5);
            const auto suffix=digestStart==std::string::npos||referenceName.size()<digestStart+66?std::string():referenceName.substr(digestStart+66);
            if((kindName!="face"&&kindName!="edge")||originalDigest.size()!=64||originalDigest.find_first_not_of("0123456789abcdef")!=std::string::npos||suffix.empty()||suffix.size()>7||suffix.find_first_not_of("0123456789")!=std::string::npos||referenceName[digestStart+65]!='.')throw std::runtime_error("Invalid reserved current-B-rep reference provenance");
            const auto* status=row.find("status"),*rowKind=row.find("kind"),*indices=row.find("indices");const size_t index=std::stoul(suffix);
            if(originalDigest==identity){
                const auto& entities=kindName=="face"?faces:edges;
                if(!rowKind||rowKind->asStr()!=kindName||index>=entities.size())throw std::runtime_error("Current-B-rep reference index provenance mismatch");
                if(status&&(status->asStr()=="bound"||status->asStr()=="merged"))if(!indices||indices->t!=V::Arr||indices->arr.size()!=1||indices->arr[0].t!=V::Num||indices->arr[0].num!=index)throw std::runtime_error("Current-B-rep reference index provenance mismatch");
                // A previously obsolete handle can bind only if its complete
                // serialized identity and encoded exact index are restored.
                V bound=V::makeArr();bound.arr.push_back(V::makeNum(index));row.set("status",V::makeStr("bound"));row.set("indices",bound);
                const V descriptor=describe(entities[index],kindName=="face"?TopAbs_FACE:TopAbs_EDGE);row.set("descriptor",descriptor);row.set("frame_available",V::makeBool(descriptor.find("normal")||descriptor.find("tangent")));
                row.obj.erase(std::remove_if(row.obj.begin(),row.obj.end(),[](const auto& item){return item.first=="reason";}),row.obj.end());
            }
            row.set("reference_scope",V::makeStr("current_brep"));row.set("persistent",V::makeBool(false));row.set("brep_sha256",V::makeStr(originalDigest));
            if(originalDigest!=identity){row.set("status",V::makeStr("obsolete"));row.set("indices",V::makeArr());row.set("reason",V::makeStr("B-rep identity changed; select the current geometry"));}
        }else if(++semanticCount>512)throw std::runtime_error("Semantic topology registry exceeds512 entries");
    }
    V mesh=result.find("mesh")?*result.find("mesh"):V{};
    for(const auto kind:{TopAbs_FACE,TopAbs_EDGE}){
        const std::string label=kind==TopAbs_FACE?"face":"edge";
        const auto& entities=kind==TopAbs_FACE?faces:edges;
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> covered,seen;
        for(const auto& row:rows.arr){const auto* rowKind=row.find("kind"),*status=row.find("status"),*indices=row.find("indices");
            if(!rowKind||rowKind->asStr()!=label||!status||(status->asStr()!="bound"&&status->asStr()!="merged"))continue;
            if(!indices||indices->t!=V::Arr||indices->arr.size()!=1||indices->arr[0].t!=V::Num||indices->arr[0].num<0||indices->arr[0].num!=std::floor(indices->arr[0].num)||indices->arr[0].num>=entities.size())throw std::runtime_error("Invalid bound selection reference index");
            covered.Add(entities[(size_t)indices->arr[0].num]);
        }
        V canonical=V::makeArr();std::vector<size_t> firstIndices;
        for(size_t index=0;index<entities.size();++index){const auto& entity=entities[index];
            const bool duplicate=seen.Contains(entity);const auto mapped=seen.Add(entity);
            if(!duplicate)firstIndices.push_back(index);canonical.arr.push_back(V::makeNum(firstIndices[(size_t)mapped-1]));
            if(duplicate||covered.Contains(entity))continue;
            if(kind==TopAbs_EDGE&&BRep_Tool::Degenerated(TopoDS::Edge(entity)))continue;
            if(rows.arr.size()>=16384)throw std::runtime_error("Selection reference inventory exceeds16384 entries; no partial inventory was published");
            const auto name="@ark."+label+"."+identity+"."+std::to_string(index);
            if(!names.insert(name).second)throw std::runtime_error("Reserved current-B-rep reference namespace collision");
            V row=V::makeObj(),indices=V::makeArr();const V descriptor=describe(entity,kind);indices.arr.push_back(V::makeNum(index));
            row.set("name",V::makeStr(name));row.set("kind",V::makeStr(label));row.set("status",V::makeStr("bound"));row.set("indices",indices);
            row.set("display_name",V::makeStr((kind==TopAbs_FACE?"Face ":"Edge ")+std::to_string(index+1)));
            row.set("reference_scope",V::makeStr("current_brep"));row.set("persistent",V::makeBool(false));row.set("brep_sha256",V::makeStr(identity));
            row.set("descriptor",descriptor);row.set("frame_available",V::makeBool(descriptor.find("normal")||descriptor.find("tangent")));rows.arr.push_back(row);
        }
        if(mesh.t==V::Obj)if(const auto* displayed=mesh.find(kind==TopAbs_FACE?"face_spans":"edges");displayed&&displayed->t==V::Arr&&displayed->arr.size()==entities.size())mesh.set(kind==TopAbs_FACE?"face_canonical":"edge_canonical",canonical);
    }
    if(mesh.t==V::Obj)result.set("mesh",mesh);
    topology.set("references",rows);topology.set("selection_inventory_complete",V::makeBool(true));topology.set("generic_reference_scope",V::makeStr("current_brep_digest"));topology.set("reference_capacity",V::makeNum(16384));result.set("topology",topology);return result;
}
V shapeResult(const TopoDS_Shape& shape,bool requireSolid){V r=V::makeObj();r.set("report",shapeReport(shape,requireSolid));r.set("brep_base64",V::makeStr(brepData(shape)));r.set("mesh",meshData(shape));return completeSelectionReferences(std::move(r));}
double symmetricDifference(const TopoDS_Shape& a,const TopoDS_Shape& b){return std::abs(properties(cut(a,b)))+std::abs(properties(cut(b,a)));}
}
