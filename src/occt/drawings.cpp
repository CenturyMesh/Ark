#include "ark/drawings.hpp"
#include "common.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepLib.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRAlgo_Projector.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <cmath>
#include <stdexcept>

namespace ark::occt {
namespace {
const V& need(const V& v,const char* key){auto p=v.find(key);if(!p)throw std::runtime_error(std::string("Missing drawing field: ")+key);return *p;}
gp_Trsf frame(const V& v){
    if(v.t!=V::Arr||v.arr.size()!=16)throw std::runtime_error("Drawing view frame requires 16 numbers");
    double m[16];for(int i=0;i<16;++i){if(v.arr[i].t!=V::Num||!std::isfinite(v.arr[i].num))throw std::runtime_error("Invalid drawing frame");m[i]=v.arr[i].num;}
    for(int a=0;a<3;++a)for(int b=a;b<3;++b){double dot=0;for(int c=0;c<3;++c)dot+=m[4*a+c]*m[4*b+c];if(std::abs(dot-(a==b?1.:0.))>1e-10)throw std::runtime_error("Drawing frame must be rigid");}
    if(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-10)throw std::runtime_error("Drawing frame must be affine");
    gp_Trsf t;t.SetValues(m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]);if(t.IsNegative())throw std::runtime_error("Drawing frame must preserve orientation");return t;
}
void append(V& rows,const TopoDS_Shape& shape,const char* visibility){
    if(shape.IsNull())return;
    size_t sampleCount=0;for(const auto& row:rows.arr)sampleCount+=need(row,"display_polyline_mm").arr.size();
    // HLR extraction publishes 2D pcurves on the projection plane. Build their
    // exact 3D representation before serialization; no mesh fitting is used.
    BRepLib::BuildCurves3d(shape);
    for(const auto& edge:shapes(shape,TopAbs_EDGE)){
        if(rows.arr.size()>=1024)throw std::runtime_error("Drawing view exceeds 1024 exact edges");
        BRepAdaptor_Curve curve(TopoDS::Edge(edge));
        if(!std::isfinite(curve.FirstParameter())||!std::isfinite(curve.LastParameter()))throw std::runtime_error("Drawing contains an unbounded curve");
        GCPnts_QuasiUniformDeflection samples(curve,.025,GeomAbs_C0);
        if(!samples.IsDone()||samples.NbPoints()<2||samples.NbPoints()>4096)throw std::runtime_error("Drawing curve preview cannot be sampled within its bound");
        sampleCount+=samples.NbPoints();if(sampleCount>65536)throw std::runtime_error("Drawing view preview exceeds 65536 samples");
        V row=V::makeObj(),points=V::makeArr();
        for(int i=1;i<=samples.NbPoints();++i){auto p=samples.Value(i);V xy=V::makeArr();xy.arr.push_back(V::makeNum(p.X()));xy.arr.push_back(V::makeNum(p.Y()));points.arr.push_back(xy);}
        row.set("visibility",V::makeStr(visibility));row.set("curve_type",V::makeNum((int)curve.GetType()));row.set("exact_brep_base64",V::makeStr(brepData(edge)));row.set("display_polyline_mm",points);rows.arr.push_back(row);
    }
}
}
V drawingViewNative(const V& request){
    const auto source=readBrep(need(request,"input_brep").str);shapeReport(source,true);
    if(shapes(source,TopAbs_SOLID).size()!=1||shapes(source,TopAbs_FACE).size()>256||shapes(source,TopAbs_EDGE).size()>1024)throw std::runtime_error("Drawing requires one valid exact solid with at most 256 faces and 1024 edges");
    const auto t=frame(need(request,"frame"));const gp_Pnt origin=gp_Pnt(0,0,0).Transformed(t);const gp_Dir normal=gp_Dir(0,0,1).Transformed(t),x=gp_Dir(1,0,0).Transformed(t);
    V edges=V::makeArr();const auto type=need(request,"type").str;
    if(type=="orthographic"){
        occ::handle<HLRBRep_Algo> algorithm=new HLRBRep_Algo;algorithm->Add(source);algorithm->Projector(HLRAlgo_Projector(gp_Ax2(origin,normal,x)));algorithm->Update();algorithm->Hide();
        HLRBRep_HLRToShape output(algorithm);append(edges,output.VCompound(),"visible");append(edges,output.OutLineVCompound(),"visible");append(edges,output.Rg1LineVCompound(),"visible");append(edges,output.HCompound(),"hidden");append(edges,output.OutLineHCompound(),"hidden");
    }else if(type=="section"){
        BRepAlgoAPI_Section cut(source,gp_Pln(origin,normal),false);cut.Approximation(false);cut.Build();
        if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Exact drawing section failed");
        append(edges,transformed(cut.Shape(),t.Inverted()),"section");
    }else throw std::runtime_error("Drawing view type is unsupported");
    if(edges.arr.empty())throw std::runtime_error("Drawing view produced no exact edges; section may miss the solid");
    V out=V::makeObj();out.set("edges",edges);out.set("geometry_authority",V::makeStr("exact_brep"));out.set("method",V::makeStr(type=="section"?"OCCT_exact_plane_section":"OCCT_HLRBRep_Algo"));out.set("preview_authority",V::makeStr("display_samples"));out.set("display_deflection_mm",V::makeNum(.025));out.set("superimposed_edges_eliminated",V::makeBool(false));out.set("units",V::makeStr("mm"));return out;
}
}
