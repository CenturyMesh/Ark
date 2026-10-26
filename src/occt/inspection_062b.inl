// Read-only inspection of the exact source. Index order is the meshData order.
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Sphere.hxx>
#include <gp_Pln.hxx>

namespace ark::occt {
static gp_XYZ inspectionVector(const V& r,const char* key){
    const auto* a=r.find(key);if(!a||a->t!=V::Arr||a->arr.size()!=3)throw std::runtime_error(std::string("Inspection requires ")+key+" [x,y,z]");
    for(const auto& n:a->arr)if(n.t!=V::Num||!std::isfinite(n.num))throw std::runtime_error("Invalid inspection coordinate");
    return {a->arr[0].num,a->arr[1].num,a->arr[2].num};
}
static TopoDS_Shape inspectionWorld(TopoDS_Shape shape,const V& r){
    if(const auto* m=r.find("placement")){
        if(m->t!=V::Arr||m->arr.size()!=16)throw std::runtime_error("Inspection placement requires 16 values");
        for(const auto& n:m->arr)if(n.t!=V::Num||!std::isfinite(n.num))throw std::runtime_error("Invalid inspection placement");
        auto at=[&](int i){return m->arr[i].num;};
        if(at(3)!=0||at(7)!=0||at(11)!=0||at(15)!=1)throw std::runtime_error("Inspection requires affine placement");
        gp_Vec x(at(0),at(1),at(2)),y(at(4),at(5),at(6)),z(at(8),at(9),at(10));
        if(std::abs(x.SquareMagnitude()-1)>1e-9||std::abs(y.SquareMagnitude()-1)>1e-9||std::abs(z.SquareMagnitude()-1)>1e-9||std::abs(x.Dot(y))>1e-9||std::abs(x.Dot(z))>1e-9||std::abs(y.Dot(z))>1e-9||x.Crossed(y).Dot(z)<0)throw std::runtime_error("Inspection requires a rigid right-handed placement");
        gp_Trsf t;t.SetValues(at(0),at(4),at(8),at(12),at(1),at(5),at(9),at(13),at(2),at(6),at(10),at(14));shape=transformed(shape,t);
    }return shape;
}
static V inspectNative062(const V& r){
    auto text=[&](const char* key){auto* v=r.find(key);if(!v||v->t!=V::Str)throw std::runtime_error(std::string("Inspection requires ")+key);return v->str;};
    auto action=text("action");auto shape=readBrep(text("brep"));V out=V::makeObj();out.set("authority",V::makeStr("exact_brep"));
    if(action=="between"){
        auto other=readBrep(text("other_brep"));BRepExtrema_DistShapeShape distance(shape,other);distance.Perform();
        if(!distance.IsDone()||distance.NbSolution()<1)throw std::runtime_error("Exact minimum distance failed");
        out.set("distance_mm",V::makeNum(distance.Value()));out.set("a",vec(distance.PointOnShape1(1).XYZ()));out.set("b",vec(distance.PointOnShape2(1).XYZ()));return out;
    }
    if(action=="section"){
        shape=inspectionWorld(shape,r);auto origin=inspectionVector(r,"origin"),n=inspectionVector(r,"normal");if(n.Modulus()<1e-12)throw std::runtime_error("Section normal has no direction");gp_Dir normal(n);gp_Pln plane(gp_Pnt(origin),normal);
        auto face=BRepBuilderAPI_MakeFace(plane).Face();auto half=BRepPrimAPI_MakeHalfSpace(face,gp_Pnt(origin+normal.XYZ())).Solid();BRepAlgoAPI_Common clip(shape,half);clip.Build();
        if(!clip.IsDone()||clip.HasErrors())throw std::runtime_error("Exact section preview failed; source unchanged");
        auto result=clip.Shape();std::vector<TopoDS_Shape> caps;double area=0;
        for(const auto& f:shapes(result,TopAbs_FACE)){BRepAdaptor_Surface surface(TopoDS::Face(f));if(surface.GetType()==GeomAbs_Plane){auto p=surface.Plane();if(p.Axis().Direction().IsParallel(normal,Precision::Angular())&&plane.Distance(p.Location())<=Precision::Confusion()){caps.push_back(f);area+=properties(f,'a');}}}
        if(!shapes(result,TopAbs_FACE).empty())out.set("mesh",meshData(result));
        if(!caps.empty())out.set("cap",meshData(compound(caps)));
        out.set("section_area_mm2",V::makeNum(area));out.set("source_unchanged",V::makeBool(true));return out;
    }
    if(action!="entity")throw std::runtime_error("Unknown inspection action");
    auto kind=text("kind");if(kind!="edge"&&kind!="face"&&kind!="vertex")throw std::runtime_error("Exact edge, face or vertex required");
    auto list=shapes(shape,kind=="edge"?TopAbs_EDGE:kind=="face"?TopAbs_FACE:TopAbs_VERTEX);auto* index=r.find("index");
    if(!index||index->t!=V::Num||!std::isfinite(index->num)||index->num<0||index->num!=std::floor(index->num)||index->num>=list.size())throw std::runtime_error("Inspection subshape index is not in this source");
    shape=inspectionWorld(list[(size_t)index->num],r);out.set("brep",V::makeStr(brepData(shape)));out.set("kind",V::makeStr(kind));
    auto direction=[&](gp_Dir d){out.set("direction",vec(d.XYZ()));};
    auto circle=[&](gp_Pnt center,gp_Dir axis,gp_Dir x,double radius){out.set("center",vec(center.XYZ()));direction(axis);out.set("diameter_mm",V::makeNum(2*radius));out.set("radius_mm",V::makeNum(radius));out.set("a",vec(center.XYZ()-x.XYZ()*radius));out.set("b",vec(center.XYZ()+x.XYZ()*radius));};
    if(kind=="edge"){
        BRepAdaptor_Curve c(TopoDS::Edge(shape));double first=c.FirstParameter(),last=c.LastParameter();
        out.set("length_mm",V::makeNum(properties(shape,'l')));out.set("a",vec(c.Value(first).XYZ()));out.set("b",vec(c.Value(last).XYZ()));out.set("type",V::makeStr("Curve"));
        if(c.GetType()==GeomAbs_Line){out.set("type",V::makeStr("Line"));direction(c.Line().Direction());}
        if(c.GetType()==GeomAbs_Circle){auto s=c.Circle();out.set("type",V::makeStr(std::abs(last-first-2*3.141592653589793)<1e-8?"Circle":"Arc"));circle(s.Location(),s.Axis().Direction(),s.XAxis().Direction(),s.Radius());}
    }else if(kind=="face"){
        BRepAdaptor_Surface s(TopoDS::Face(shape));GProp_GProps props;BRepGProp::SurfaceProperties(shape,props);out.set("area_mm2",V::makeNum(props.Mass()));out.set("center",vec(props.CentreOfMass().XYZ()));out.set("type",V::makeStr("Surface"));
        if(s.GetType()==GeomAbs_Plane){out.set("type",V::makeStr("Plane"));direction(s.Plane().Axis().Direction());}
        if(s.GetType()==GeomAbs_Cylinder){auto c=s.Cylinder();out.set("type",V::makeStr("Cylinder"));circle(c.Location(),c.Axis().Direction(),c.XAxis().Direction(),c.Radius());}
        if(s.GetType()==GeomAbs_Sphere){auto c=s.Sphere();out.set("type",V::makeStr("Sphere"));circle(c.Location(),c.Position().Direction(),c.Position().XDirection(),c.Radius());}
    }else{out.set("type",V::makeStr("Vertex"));out.set("center",vec(BRep_Tool::Pnt(TopoDS::Vertex(shape)).XYZ()));}
    return out;
}
}
