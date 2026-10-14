#include "ark/drawings.hpp"
#include "common.hpp"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepLib.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRAlgo_Projector.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
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
void append(V& rows,const TopoDS_Shape& shape,const char* visibility,const V& source){
    if(shape.IsNull())return;
    size_t sampleCount=0;for(const auto& row:rows.arr)sampleCount+=need(row,"display_polyline_mm").arr.size();
    // HLR extraction publishes 2D pcurves on the projection plane. Build their
    // exact 3D representation before serialization; no mesh fitting is used.
    BRepLib::BuildCurves3d(shape);
    for(const auto& edge:shapes(shape,TopAbs_EDGE)){
        if(rows.arr.size()>=4096)throw std::runtime_error("Drawing view exceeds 4096 exact edges");
        BRepAdaptor_Curve curve(TopoDS::Edge(edge));
        if(!std::isfinite(curve.FirstParameter())||!std::isfinite(curve.LastParameter()))throw std::runtime_error("Drawing contains an unbounded curve");
        GCPnts_QuasiUniformDeflection samples(curve,.025,GeomAbs_C0);
        if(!samples.IsDone()||samples.NbPoints()<2||samples.NbPoints()>4096)throw std::runtime_error("Drawing curve preview cannot be sampled within its bound");
        sampleCount+=samples.NbPoints();if(sampleCount>65536)throw std::runtime_error("Drawing view preview exceeds 65536 samples");
        V row=V::makeObj(),points=V::makeArr();
        for(int i=1;i<=samples.NbPoints();++i){auto p=samples.Value(i);V xy=V::makeArr();xy.arr.push_back(V::makeNum(p.X()));xy.arr.push_back(V::makeNum(p.Y()));points.arr.push_back(xy);}
        row.set("visibility",V::makeStr(visibility));row.set("curve_type",V::makeNum((int)curve.GetType()));row.set("exact_brep_base64",V::makeStr(brepData(edge)));row.set("display_polyline_mm",points);row.set("source",source);row.set("curve_id",V::makeStr(sha256(source.dump()+brepData(edge))));const auto b=bounds(edge);V box=V::makeArr();box.arr={V::makeNum(b[0]),V::makeNum(b[1]),V::makeNum(b[3]),V::makeNum(b[4])};row.set("exact_bounds_mm",box);rows.arr.push_back(row);
    }
}
}
V drawingViewNative(const V& request){
    struct Occurrence{TopoDS_Shape shape;V source;};std::vector<Occurrence> occurrences;
    if(const auto* rows=request.find("occurrences")){if(rows->t!=V::Arr||rows->arr.empty()||rows->arr.size()>256)throw std::runtime_error("Drawing requires 1..256 exact occurrences");for(const auto& row:rows->arr)occurrences.push_back({transformed(readBrep(need(row,"input_brep").str),frame(need(row,"frame"))),need(row,"source")});}
    else {V source=V::makeObj();source.set("feature",V::makeNum(0));source.set("node",V::makeNum(0));source.set("copy",V::makeNum(0));source.set("path",V::makeStr("native_input"));occurrences.push_back({readBrep(need(request,"input_brep").str),source});}
    size_t faces=0,sourceEdges=0;for(const auto& occurrence:occurrences){shapeReport(occurrence.shape,false);const auto solids=shapes(occurrence.shape,TopAbs_SOLID);if(solids.empty())throw std::runtime_error("Drawing source occurrence requires exact positive-volume solids");for(const auto& solid:solids)if(properties(solid)<=0)throw std::runtime_error("Drawing source contains a nonpositive-volume solid");faces+=shapes(occurrence.shape,TopAbs_FACE).size();sourceEdges+=shapes(occurrence.shape,TopAbs_EDGE).size();}if(faces>4096||sourceEdges>16384)throw std::runtime_error("Drawing source exceeds 4096 faces or 16384 edges");
    const auto t=frame(need(request,"frame"));const gp_Pnt origin=gp_Pnt(0,0,0).Transformed(t);const gp_Dir normal=gp_Dir(0,0,1).Transformed(t),x=gp_Dir(1,0,0).Transformed(t);
    V edges=V::makeArr(),hatches=V::makeArr(),centres=V::makeArr(),areas=V::makeArr();const auto type=need(request,"type").str;
    if(type=="orthographic"){
        occ::handle<HLRBRep_Algo> algorithm=new HLRBRep_Algo;for(const auto& occurrence:occurrences)algorithm->Add(occurrence.shape);algorithm->Projector(HLRAlgo_Projector(gp_Ax2(origin,normal,x)));algorithm->Update();algorithm->Hide();HLRBRep_HLRToShape output(algorithm);
        // Filter one common HLR solve by its source shape: exact inter-part
        // occlusion and explicit identity for every placed pattern copy.
        for(const auto& occurrence:occurrences){const auto& shape=occurrence.shape;const auto& source=occurrence.source;append(edges,output.VCompound(shape),"visible",source);append(edges,output.OutLineVCompound(shape),"visible",source);append(edges,output.Rg1LineVCompound(shape),"visible",source);append(edges,output.HCompound(shape),"hidden",source);append(edges,output.OutLineHCompound(shape),"hidden",source);}
        for(const auto& row:edges.arr){if(need(row,"visibility").str=="hidden"||need(row,"curve_type").num!=GeomAbs_Circle)continue;const auto edge=readBrep(need(row,"exact_brep_base64").str);BRepAdaptor_Curve curve(TopoDS::Edge(edge));const auto circle=curve.Circle();const auto p=circle.Location();const double length=circle.Radius()+2.;const auto& source=need(row,"source");append(centres,BRepBuilderAPI_MakeEdge(gp_Pnt(p.X()-length,p.Y(),0),gp_Pnt(p.X()+length,p.Y(),0)).Edge(),"centreline",source);append(centres,BRepBuilderAPI_MakeEdge(gp_Pnt(p.X(),p.Y()-length,0),gp_Pnt(p.X(),p.Y()+length,0)).Edge(),"centreline",source);}
    }else if(type=="section"){
        for(size_t index=0;index<occurrences.size();++index){const auto& occurrence=occurrences[index];BRepAlgoAPI_Section cut(occurrence.shape,gp_Pln(origin,normal),false);cut.Approximation(false);cut.Build();if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("Exact drawing section failed");const auto cutEdges=transformed(cut.Shape(),t.Inverted());if(shapes(cutEdges,TopAbs_EDGE).empty())continue;append(edges,cutEdges,"section",occurrence.source);
            // A solid/plane common retains its actual planar domains and holes.
            // Each hatch edge is then clipped by that exact domain.
            const auto local=transformed(occurrence.shape,t.Inverted());const auto box=bounds(local);const auto plane=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),box[0]-1,box[3]+1,box[1]-1,box[4]+1).Face();BRepAlgoAPI_Common common(local,plane);common.Build();if(!common.IsDone()||common.HasErrors())throw std::runtime_error("Exact section region failed");const auto region=common.Shape();if(shapes(region,TopAbs_FACE).empty())throw std::runtime_error("Section contour has no positive planar region for hatching");const double area=properties(region,'a');if(!(area>0))throw std::runtime_error("Section area is not positive");
            const double xmin=box[0],xmax=box[3],ymin=box[1],ymax=box[4],pitch=std::max(2.5,(xmax-xmin+ymax-ymin)/90.);const bool reverse=index%2!=0;const double cmin=reverse?xmin+ymin:ymin-xmax,cmax=reverse?xmax+ymax:ymax-xmin;for(double c=std::ceil(cmin/pitch)*pitch;c<cmax;c+=pitch){auto line=BRepBuilderAPI_MakeEdge(gp_Pnt(xmin-1,reverse?c-xmin+1:xmin-1+c,0),gp_Pnt(xmax+1,reverse?c-xmax-1:xmax+1+c,0)).Edge();BRepAlgoAPI_Common clip(region,line);clip.Build();if(!clip.IsDone()||clip.HasErrors())throw std::runtime_error("Exact hatch clipping failed");append(hatches,clip.Shape(),"hatch",occurrence.source);}
            V row=V::makeObj();row.set("source",occurrence.source);row.set("area_mm2",V::makeNum(area));row.set("hatch_pitch_mm",V::makeNum(pitch));row.set("hatch_angle_deg",V::makeNum(reverse?-45:45));row.set("authority",V::makeStr("exact_solid_plane_common"));areas.arr.push_back(row);
        }
    }else throw std::runtime_error("Drawing view type is unsupported");
    if(edges.arr.empty())throw std::runtime_error("Drawing view produced no exact edges; section may miss every source solid");double xmin=1e100,ymin=1e100,xmax=-1e100,ymax=-1e100;for(const auto& edge:edges.arr){const auto& box=need(edge,"exact_bounds_mm");xmin=std::min(xmin,box.arr[0].num);ymin=std::min(ymin,box.arr[1].num);xmax=std::max(xmax,box.arr[2].num);ymax=std::max(ymax,box.arr[3].num);}V box=V::makeArr();box.arr={V::makeNum(xmin),V::makeNum(ymin),V::makeNum(xmax),V::makeNum(ymax)};
    V out=V::makeObj();out.set("edges",edges);out.set("hatches",hatches);out.set("centrelines",centres);out.set("section_regions",areas);out.set("projected_bounds_mm",box);out.set("geometry_authority",V::makeStr("exact_brep"));out.set("method",V::makeStr(type=="section"?"OCCT_exact_plane_section":"OCCT_HLRBRep_Algo"));out.set("preview_authority",V::makeStr("display_samples"));out.set("display_deflection_mm",V::makeNum(.025));out.set("source_occurrences_fused",V::makeBool(false));out.set("superimposed_edges_eliminated",V::makeBool(false));out.set("units",V::makeStr("mm"));return out;
}
}