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
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Standard_Version.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <Poly_Triangulation.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <GeomAbs_CurveType.hxx>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
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
double properties(const TopoDS_Shape& shape,char kind){GProp_GProps props;if(shape.IsNull())return 0;if(kind=='a')BRepGProp::SurfaceProperties(shape,props);else if(kind=='l')BRepGProp::LinearProperties(shape,props);else BRepGProp::VolumeProperties(shape,props);return props.Mass();}
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
V meshData(const TopoDS_Shape& s,double deflection,double angle){
    BRepMesh_IncrementalMesh mesher(s,deflection,false,angle,false);mesher.Perform();if(!mesher.IsDone())throw std::runtime_error("Native preview tessellation failed");V pos=V::makeArr(),idx=V::makeArr(),spans=V::makeArr(),edges=V::makeArr();
    for(auto& item:shapes(s,TopAbs_FACE)){auto face=TopoDS::Face(item);TopLoc_Location loc;auto tri=BRep_Tool::Triangulation(face,loc);if(tri.IsNull())throw std::runtime_error("Native face has no triangulation");size_t base=pos.arr.size()/3,start=idx.arr.size()/3;
        for(int i=1;i<=tri->NbNodes();++i){auto p=tri->Node(i).Transformed(loc.Transformation());for(auto v:{p.X(),p.Y(),p.Z()})pos.arr.push_back(V::makeNum(std::round(v*1e8)/1e8));}
        for(int i=1;i<=tri->NbTriangles();++i){int a,b,c;tri->Triangle(i).Get(a,b,c);if(face.Orientation()==TopAbs_REVERSED)std::swap(b,c);for(int v:{a,b,c})idx.arr.push_back(V::makeNum((double)(base+v-1)));}
        V span=V::makeArr();span.arr.push_back(V::makeNum((double)start));span.arr.push_back(V::makeNum((double)(idx.arr.size()/3-start)));spans.arr.push_back(std::move(span));
        if(pos.arr.size()>6000000||idx.arr.size()>6000000)throw std::runtime_error("Native preview exceeds mesh size limit");
    }
    for(auto& item:shapes(s,TopAbs_EDGE)){BRepAdaptor_Curve curve(TopoDS::Edge(item));double a=curve.FirstParameter(),b=curve.LastParameter();int steps=curve.GetType()==GeomAbs_Line?1:12;if(!std::isfinite(a)||!std::isfinite(b)||a==b)steps=1;V poly=V::makeArr();
        for(int i=0;i<=steps;++i){gp_Pnt p=curve.Value(a+(b-a)*(steps?i/(double)steps:0));for(double v:{p.X(),p.Y(),p.Z()})poly.arr.push_back(V::makeNum(std::round(v*1e8)/1e8));}
        edges.arr.push_back(std::move(poly));
    }
    V mesh=V::makeObj();mesh.set("pos",pos);mesh.set("idx",idx);mesh.set("face_spans",spans);mesh.set("edges",edges);return mesh;
}
std::string brepData(const TopoDS_Shape& shape){std::ostringstream stream;BRepTools::Write(shape,stream,false,false,TopTools_FormatVersion_CURRENT);return base64Encode(stream.str());}
TopoDS_Shape readBrep(const std::string& encoded){std::istringstream stream(base64Decode(encoded));TopoDS_Shape shape;BRep_Builder b;BRepTools::Read(shape,stream,b);if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())throw std::runtime_error("Invalid native B-rep payload");return shape;}
std::string kernelVersion(){return OCC_VERSION_COMPLETE;}
V shapeReport(const TopoDS_Shape& shape,bool requireSolid){
    if(shape.IsNull()||!BRepCheck_Analyzer(shape).IsValid())throw std::runtime_error("Native result failed B-rep validation");auto solids=shapes(shape,TopAbs_SOLID);if(requireSolid&&(solids.size()!=1||properties(shape)<=0))throw std::runtime_error("Native operation must produce one positive-volume solid");
    V report=V::makeObj();report.set("kernel",V::makeStr("OCCT"));report.set("kernel_version",V::makeStr(kernelVersion()));report.set("brep_valid",V::makeBool(true));report.set("solids",V::makeNum(solids.size()));report.set("faces",V::makeNum(shapes(shape,TopAbs_FACE).size()));report.set("area_mm2",V::makeNum(properties(shape,'a')));report.set("volume_mm3",solids.empty()?V{}:V::makeNum(properties(shape)));report.set("display_mesh_authoritative",V::makeBool(false));report.set("self_intersections_checked",V::makeBool(false));report.set("manufacturing_authoritative",V::makeBool(false));
    V b=V::makeArr();for(auto x:bounds(shape))b.arr.push_back(V::makeNum(x));report.set("bounds_mm",b);return report;
}
V shapeResult(const TopoDS_Shape& shape,bool requireSolid){V r=V::makeObj();r.set("report",shapeReport(shape,requireSolid));r.set("brep_base64",V::makeStr(brepData(shape)));r.set("mesh",meshData(shape));return r;}
double symmetricDifference(const TopoDS_Shape& a,const TopoDS_Shape& b){return std::abs(properties(cut(a,b)))+std::abs(properties(cut(b,a)));}
}
