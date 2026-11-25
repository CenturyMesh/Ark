#include "ark/occt_modeling.hpp"
#include "ark/occt_solid.hpp"
#include "common.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <GProp_GProps.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_Plane.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_Array2.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <BRep_Builder.hxx>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ark::occt;
// Every fixture here is synthetic; the references are closed forms or a
// quadrature that shares neither code, order nor partition with the product.
// Accuracy is always asserted on piecewiseArea() against those references, never
// through OCCT's verdict. OCCT's verdict only decides what properties() must
// return: its own area bit for bit, or the piecewise one.
namespace {
int checks=0,failures=0;
constexpr const char* kRejection="Exact surface area integration did not meet its relative precision";
constexpr double kGate=1e-10,kRobust=100; // A verdict counts as robust at least this factor away from the gate.
// A failed check is recorded and the run continues: one run must report every
// fixture on a toolchain that cannot be debugged locally.
bool check(bool value,const std::string& why){++checks;if(!value){++failures;std::cout<<"FAILED: "<<why<<std::endl;}return value;}
void require(bool value,const std::string& why){if(!check(value,why))throw std::runtime_error("cannot continue after: "+why);}
void relative(double actual,double expected,double tolerance,const std::string& why){
    if(check(std::abs(actual-expected)<=tolerance*std::abs(expected),why))return;
    std::cout<<std::setprecision(17)<<"  actual="<<actual<<" expected="<<expected<<" relative="<<(actual-expected)/expected<<" tolerance="<<tolerance<<std::endl;
}
std::string rejection(const std::function<void()>& action){try{action();}catch(const std::exception& error){return error.what();}return {};}
double area(const TopoDS_Shape& shape){return properties(shape,'a');}

// The OCCT measurement and gate that decide whether a shape needs the piecewise
// integration. A passing shape must keep exactly this value.
struct Occt {double area=0,error=0;bool passes=false;double margin()const{return error/(kGate*std::abs(area));}};
Occt occt(const TopoDS_Shape& shape){
    GProp_GProps total;Occt out;
    for(const auto& face:shapes(shape,TopAbs_FACE)){
        GProp_GProps part;const auto type=BRepAdaptor_Surface(TopoDS::Face(face)).GetType();
        if(type<=GeomAbs_Torus)BRepGProp::SurfaceProperties(face,part);
        else{
            // The call fills part, so it must be sequenced before part.Mass().
            const double estimate=BRepGProp::SurfaceProperties(face,part,1e-14,false);out.error+=estimate*std::abs(part.Mass());
        }
        total.Add(part);
    }
    out.area=total.Mass();out.passes=out.error<=kGate*std::abs(out.area);return out;
}
enum class Verdict {Either,RobustMiss,RobustPass};
// Reports one fixture, checks the piecewise area against its reference and that
// properties() dispatches on OCCT's verdict, whichever it is here.
Occt verify(const std::string& name,const TopoDS_Shape& shape,double expected,double tolerance,Verdict verdict){
    const auto native=occt(shape);double piecewise=0,published=0;
    const auto refused=rejection([&]{piecewise=piecewiseArea(shape);}),unpublished=rejection([&]{published=area(shape);});
    std::cout<<std::setprecision(17)<<"FIXTURE "<<name<<": occt="<<native.area<<std::setprecision(6)<<" estimate/gate="<<native.margin()<<" verdict="<<(native.passes?"pass":"miss")<<" occt-error="<<std::abs(native.area-expected)/std::abs(expected)<<std::setprecision(17)<<" piecewise="<<piecewise<<std::setprecision(6)<<" piecewise-error="<<std::abs(piecewise-expected)/std::abs(expected)<<(refused.empty()?"":" piecewise-rejected=\""+refused+"\"")<<(unpublished.empty()?"":" properties-rejected=\""+unpublished+"\"")<<std::endl;
    check(refused.empty()&&unpublished.empty(),name+": neither area may be rejected");
    relative(piecewise,expected,tolerance,name+": piecewise area against its reference");
    if(native.passes)check(published==native.area,name+": a passing shape keeps the OCCT area bit for bit");
    else check(published==piecewise,name+": a missed estimate publishes the piecewise area");
    if(verdict==Verdict::RobustMiss)check(!native.passes&&native.margin()>=kRobust,name+": the estimate must miss the gate by a wide margin (failure class under test)");
    if(verdict==Verdict::RobustPass)check(native.passes&&native.margin()<=1/kRobust,name+": the estimate must pass the gate by a wide margin");
    return native;
}

Handle(Geom_BSplineSurface) bspline(const std::vector<std::vector<gp_Pnt>>& poles,const std::vector<double>& uKnots,const std::vector<int>& uMults,int uDegree,const std::vector<double>& vKnots,const std::vector<int>& vMults,int vDegree){
    NCollection_Array2<gp_Pnt> p(1,(int)poles.size(),1,(int)poles[0].size());for(int i=1;i<=p.ColLength();++i)for(int j=1;j<=p.RowLength();++j)p(i,j)=poles[(size_t)i-1][(size_t)j-1];
    auto reals=[](const std::vector<double>& v){NCollection_Array1<double> out(1,(int)v.size());for(size_t i=0;i<v.size();++i)out((int)i+1)=v[i];return out;};
    auto integers=[](const std::vector<int>& v){NCollection_Array1<int> out(1,(int)v.size());for(size_t i=0;i<v.size();++i)out((int)i+1)=v[i];return out;};
    return new Geom_BSplineSurface(p,reals(uKnots),reals(vKnots),integers(uMults),integers(vMults),uDegree,vDegree);
}
// Straight p-curves, as native offset and shell operations leave them: one OCCT
// interval per edge however many knots of the support it crosses.
TopoDS_Edge segment(const Handle(Geom_Surface)& surface,gp_Pnt2d a,gp_Pnt2d b){const gp_Vec2d d(a,b);return BRepBuilderAPI_MakeEdge(new Geom2d_Line(a,gp_Dir2d(d)),surface,0.,d.Magnitude()).Edge();}
TopoDS_Face faceOf(const Handle(Geom_Surface)& surface,BRepBuilderAPI_MakeWire& wire){
    require(wire.IsDone(),"fixture wire");BRepBuilderAPI_MakeFace maker(surface,wire.Wire(),true);require(maker.IsDone(),"fixture face");BRepLib::BuildCurves3d(maker.Face(),1e-7);return maker.Face();
}
TopoDS_Face boxed(const Handle(Geom_Surface)& surface){
    double u1,u2,v1,v2;surface->Bounds(u1,u2,v1,v2);BRepBuilderAPI_MakeWire wire;
    wire.Add(segment(surface,{u1,v1},{u2,v1}));wire.Add(segment(surface,{u2,v1},{u2,v2}));wire.Add(segment(surface,{u2,v2},{u1,v2}));wire.Add(segment(surface,{u1,v2},{u1,v1}));return faceOf(surface,wire);
}

// C1 quadratic profile (control polygon in YZ) swept straight along X. The
// curvature jumps at its knots, hence so does the area density of its offset.
const std::vector<std::array<double,2>> kProfile{{0,0},{6,0},{13,4},{17,11},{26,9},{33,15}};
const std::vector<double> kProfileKnots{0,1,2.2,3.1,4.5};
constexpr double kWidth=25;
Handle(Geom_BSplineSurface) sweptProfile(double shift=0){
    std::vector<std::vector<gp_Pnt>> poles(2);for(int side=0;side<2;++side)for(const auto& p:kProfile)poles[(size_t)side].push_back(gp_Pnt(shift+side*kWidth,shift+p[0],p[1]));
    return bspline(poles,{0,1},{2,2},1,kProfileKnots,{3,1,1,1,3},2);
}
// Planar offset curves satisfy L(d) = L - d * turning. Both are closed forms on
// each parabolic span, so the swept offset area is known exactly.
double sweptOffsetArea(double distance){
    Handle(Geom_BSplineSurface) basis=sweptProfile();double length=0,turning=0;
    for(size_t span=0;span+1<kProfileKnots.size();++span){
        const double first=kProfileKnots[span],step=kProfileKnots[span+1]-first;
        auto derivative=[&](double s){gp_Pnt at;gp_Vec du,dv;basis->D1(.5,first+step*s,at,du,dv);return std::array<double,2>{dv.Y()*step,dv.Z()*step};};
        // c'(s)=a+b*s from interior values, so no knot side is involved.
        const auto p=derivative(.25),q=derivative(.75);const double by=2*(q[0]-p[0]),bz=2*(q[1]-p[1]),ay=p[0]-.25*by,az=p[1]-.25*bz;
        const double A=by*by+bz*bz,B=ay*by+az*bz,C=ay*ay+az*az;
        auto primitive=[&](double s){const double speed=std::sqrt(A*s*s+2*B*s+C);return (A*s+B)*speed/(2*A)+(A*C-B*B)/(2*A*std::sqrt(A))*std::log((A*s+B)/std::sqrt(A)+speed);};
        length+=primitive(1)-primitive(0);turning+=std::atan2(ay*(az+bz)-az*(ay+by),ay*(ay+by)+az*(az+bz));
    }
    return kWidth*(length-distance*turning);
}

// Doubly curved C1 biquadratic basis with knots in both directions.
Handle(Geom_BSplineSurface) doublyCurved(){
    std::vector<std::vector<gp_Pnt>> poles;const double xs[]={0,5,14,22,30},ys[]={0,7,16,25,36};
    for(int i=0;i<5;++i){poles.emplace_back();for(int j=0;j<5;++j)poles.back().push_back(gp_Pnt(xs[i]+.6*j*(i%2),ys[j]-.4*i,3*std::sin(.8*i+.3)+4*std::cos(.7*j)+.9*std::sin(1.1*i*j)));}
    return bspline(poles,{0,.8,2.1,3},{3,1,1,3},2,{0,1.3,2,3.2},{3,1,1,3},2);
}
// Lower boundary in UV: a degree-one B-spline p-curve (C0, as left behind by
// Boolean and offset operations) that crosses knot lines of the support.
const std::vector<std::array<double,2>> kTrim{{0,.4},{.5,1.6},{1.1,.9},{1.7,2.3},{2.4,1.1},{3,.2}};
TopoDS_Face trimmed(const Handle(Geom_Surface)& surface){
    double u1,u2,v1,v2;surface->Bounds(u1,u2,v1,v2);const int n=(int)kTrim.size();
    NCollection_Array1<gp_Pnt2d> poles(1,n);NCollection_Array1<double> knots(1,n);NCollection_Array1<int> mults(1,n);
    for(int i=1;i<=n;++i){poles(i)=gp_Pnt2d(kTrim[(size_t)i-1][0],kTrim[(size_t)i-1][1]);knots(i)=i-1;mults(i)=(i==1||i==n)?2:1;}
    BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(Handle(Geom2d_Curve)(new Geom2d_BSplineCurve(poles,knots,mults,1)),surface).Edge());
    wire.Add(segment(surface,{u2,kTrim.back()[1]},{u2,v2}));wire.Add(segment(surface,{u2,v2},{u1,v2}));wire.Add(segment(surface,{u1,v2},{u1,kTrim.front()[1]}));return faceOf(surface,wire);
}
// Reference: the opposite integration order (outer u, inner v), composite
// 12-point Gauss on fixed halvings of the exact smooth cells. Compared at two
// resolutions by the caller.
double trimmedReference(const Handle(Geom_Surface)& surface,const std::vector<double>& uKnots,const std::vector<double>& vKnots,int halvings){
    static const double x[]={.125233408511468915,.367831498998180194,.587317954286617447,.769902674194304687,.904117256370474857,.981560634246719251},w[]={.249147045813402785,.233492536538354809,.203167426723065922,.160078328543346226,.106939325995318431,.047175336386511827};
    auto gauss=[&](const std::function<double(double)>& f,double a,double b){double sum=0;for(int part=0;part<halvings;++part){const double h=(b-a)/halvings,c=a+h*(part+.5);for(int i=0;i<6;++i)sum+=w[i]*(f(c-.5*h*x[i])+f(c+.5*h*x[i]))*.5*h;}return sum;};
    auto lower=[&](double u){for(size_t i=0;i+1<kTrim.size();++i)if(u<=kTrim[i+1][0]||i+2==kTrim.size()){const double s=(u-kTrim[i][0])/(kTrim[i+1][0]-kTrim[i][0]);return kTrim[i][1]+s*(kTrim[i+1][1]-kTrim[i][1]);}return 0.;};
    std::vector<double> cuts=uKnots;
    for(size_t i=0;i+1<kTrim.size();++i){cuts.push_back(kTrim[i][0]);for(double v:vKnots){const double s=(v-kTrim[i][1])/(kTrim[i+1][1]-kTrim[i][1]);if(s>0&&s<1)cuts.push_back(kTrim[i][0]+s*(kTrim[i+1][0]-kTrim[i][0]));}}
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double a,double b){return b-a<1e-14;}),cuts.end());
    double total=0;
    for(size_t i=0;i+1<cuts.size();++i)total+=gauss([&](double u){
        std::vector<double> limits{lower(u)};for(double v:vKnots)if(v>limits.front())limits.push_back(v);double sum=0;
        for(size_t j=0;j+1<limits.size();++j)sum+=gauss([&](double v){gp_Pnt at;gp_Vec du,dv;surface->D1(u,v,at,du,dv);return du.Crossed(dv).Magnitude();},limits[j],limits[j+1]);
        return sum;
    },cuts[i],cuts[i+1]);
    return total;
}

void passingShapesKeepTheirExactArea(){
    const double pi=std::acos(-1.);
    const auto box=BRepPrimAPI_MakeBox(20,30,40).Shape(),cylinder=BRepPrimAPI_MakeCylinder(7,19).Shape(),cone=BRepPrimAPI_MakeCone(9,4,13).Shape(),sphere=BRepPrimAPI_MakeSphere(6).Shape(),torus=BRepPrimAPI_MakeTorus(12,3).Shape();
    gp_Trsf shift;shift.SetTranslation(gp_Vec(10,15,-5));const auto drilled=cut(box,transformed(BRepPrimAPI_MakeCylinder(4,60).Shape(),shift));
    struct Solid {const char* name;TopoDS_Shape shape;double area;};
    const std::vector<Solid> solids{{"box",box,5200},{"cylinder",cylinder,364*pi},{"cone",cone,pi*(81+16+13*std::sqrt(25.+169.))},{"sphere",sphere,144*pi},{"torus",torus,144*pi*pi},{"drilled box",drilled,5200+288*pi}};
    size_t spline=0;
    for(const auto& solid:solids){
        verify(solid.name,solid.shape,solid.area,1e-12,Verdict::RobustPass);
        if(solid.shape.IsSame(torus))continue;
        const auto converted=BRepBuilderAPI_NurbsConvert(solid.shape,true).Shape();verify(std::string("rational ")+solid.name,converted,solid.area,1e-11,Verdict::RobustPass);
        for(const auto& face:shapes(converted,TopAbs_FACE))spline+=BRepAdaptor_Surface(TopoDS::Face(face)).GetType()==GeomAbs_BSplineSurface;
    }
    check(spline>=20,"unchanged-area corpus covers spline supports");
    verify("torus and rational sphere",compound({torus,BRepBuilderAPI_NurbsConvert(sphere,true).Shape()}),144*pi*pi+144*pi,1e-11,Verdict::RobustPass);
    verify("C1 sheet without offset",boxed(sweptProfile()),sweptOffsetArea(0),1e-11,Verdict::Either);
}
void knotJumpsOfAnOffset(){
    // Failure class: straight p-curves over an offset of a C1 spline. The outer
    // integrand jumps at knots that the OCCT rule does not split.
    for(double distance:{1.5,-1.5,-3.}){
        const std::string name="swept offset "+std::to_string(distance);const auto face=boxed(new Geom_OffsetSurface(sweptProfile(),distance));
        const double expected=sweptOffsetArea(distance);verify(name,face,expected,1e-11,Verdict::RobustMiss);
        const double measured=piecewiseArea(face);check(piecewiseArea(face)==measured,name+": area is deterministic");check(piecewiseArea(face.Reversed())==measured,name+": orientation does not change the area");
        gp_Trsf place;place.SetRotation(gp_Ax1(gp_Pnt(3,1,2),gp_Dir(1,2,3)),.7);place.SetTranslationPart(gp_Vec(40,-25,60));verify(name+" placed",transformed(face,place),expected,1e-11,Verdict::RobustMiss);
        gp_Trsf scale;scale.SetScale(gp_Pnt(1,2,3),2);verify(name+" scaled",transformed(face,scale),4*expected,1e-11,Verdict::RobustMiss);
    }
}
void trimmedOffset(){
    // Both the support and the trim have knots, and the inner integral needs
    // refinement. OCCT's estimate is close to the gate: either verdict is valid.
    const auto basis=doublyCurved();
    for(double distance:{-.8,.6}){
        const std::string name="trimmed doubly curved offset "+std::to_string(distance);
        Handle(Geom_Surface) surface=new Geom_OffsetSurface(basis,distance);const auto face=trimmed(surface);const std::vector<double> u{0,.8,2.1,3},v{0,1.3,2,3.2};
        const double coarse=trimmedReference(surface,u,v,4),fine=trimmedReference(surface,u,v,8);relative(coarse,fine,1e-12,name+": independent reference is converged");
        const auto native=verify(name,face,fine,1e-11,Verdict::Either);
        check(std::abs(native.area-fine)>1e-6*fine,name+": the OCCT area is imprecise whatever it estimates");
    }
}
// Planar face bounded by one C0 chain of cubic spans; exact area by Green's
// theorem, whose integrand is a degree-five polynomial on each span.
TopoDS_Face splinePlate(double& exact){
    const std::vector<std::array<double,2>> p{{0,0},{4,-3},{9,5},{14,0},{19,4},{12,9},{15,15},{9,11},{7,21},{1,14},{-5,9},{5,5},{0,0}};
    const int spans=((int)p.size()-1)/3;NCollection_Array1<gp_Pnt> poles(1,(int)p.size());NCollection_Array1<double> knots(1,spans+1);NCollection_Array1<int> mults(1,spans+1);
    for(int i=1;i<=(int)p.size();++i)poles(i)=gp_Pnt(200+p[(size_t)i-1][0],p[(size_t)i-1][1],0);
    for(int i=1;i<=spans+1;++i){knots(i)=i-1;mults(i)=(i==1||i==spans+1)?4:3;}
    Handle(Geom_BSplineCurve) curve=new Geom_BSplineCurve(poles,knots,mults,3);exact=0;
    const double x[]={0,.538469310105683091,.906179845938663993},w[]={.568888888888888889,.478628670499366468,.236926885056189088};
    for(int i=0;i<spans;++i)for(int k=-2;k<=2;++k){gp_Pnt at;gp_Vec d;curve->D1(i+.5+.5*(k<0?-x[-k]:x[k]),at,d);exact+=.5*w[std::abs(k)]*.5*((at.X()-200)*d.Y()-at.Y()*d.X());}
    BRepBuilderAPI_MakeFace maker(new Geom_Plane(gp_Pnt(200,0,0),gp_Dir(0,0,1)),BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(curve).Edge()).Wire(),true);require(maker.IsDone(),"spline plate");exact=std::abs(exact);return maker.Face();
}
void everyFaceOfAMissedShapeIsIntegratedAgain(){
    double exact=0;const auto plate=splinePlate(exact);check(BRepCheck_Analyzer(plate).IsValid(),"spline plate fixture is valid");
    verify("spline-trimmed plate",plate,exact,1e-11,Verdict::RobustPass); // Elementary: no estimate, existing area.
    verify("offset, spline-trimmed plate and box",compound({boxed(new Geom_OffsetSurface(sweptProfile(),1.5)),plate,BRepPrimAPI_MakeBox(2,3,4).Shape()}),sweptOffsetArea(1.5)+exact+52,1e-11,Verdict::RobustMiss);
}
TopoDS_Wire section(double z,double a,double b){
    const double k=.5522847498*1.25;const gp_Pnt c[]={{a,0,z},{0,b,z},{-a,0,z},{0,-b,z}};const double t[][2]={{0,b},{-a,0},{0,-b},{a,0}};
    BRepBuilderAPI_MakeWire wire;for(int i=0;i<4;++i){const int j=(i+1)%4;NCollection_Array1<gp_Pnt> p(1,4);p(1)=c[i];p(2)=c[i].Translated(gp_Vec(t[i][0]*k,t[i][1]*k,0));p(3)=c[j].Translated(gp_Vec(-t[j][0]*k,-t[j][1]*k,0));p(4)=c[j];wire.Add(BRepBuilderAPI_MakeEdge(Handle(Geom_Curve)(new Geom_BezierCurve(p))).Edge());}
    require(wire.IsDone(),"loft section");return wire.Wire();
}
std::string planarFaceAt(const V& body,const TopoDS_Shape& shape,double z){
    const auto faces=shapes(shape,TopAbs_FACE);
    for(const auto& row:body.find("topology")->find("references")->arr)if(row.find("kind")->str=="face"&&row.find("status")->str=="bound"&&row.find("indices")->arr.size()==1){
        const auto& face=faces[(size_t)row.find("indices")->arr[0].num];const auto box=bounds(face);
        if(BRepAdaptor_Surface(TopoDS::Face(face)).GetType()==GeomAbs_Plane&&std::abs(box[2]-z)<1e-5&&std::abs(box[5]-z)<1e-5)return row.find("name")->str;
    }
    throw std::runtime_error("fixture has no referenced planar face");
}
void shelledLoft(){
    // The modelling route that produces this class: a shelled freeform loft.
    BRepOffsetAPI_ThruSections loft(true,false,1e-6);loft.CheckCompatibility(false);loft.SetMaxDegree(3);
    loft.AddWire(section(0,24,15));loft.AddWire(section(16,27,19));loft.AddWire(section(34,20,14));loft.AddWire(section(48,22,11));loft.Build();require(loft.IsDone(),"loft fixture");
    const auto body=completeSelectionReferences(shapeResult(loft.Shape(),true));const auto solid=readBrep(body.find("brep_base64")->str);
    V openings=V::makeArr();for(const auto& end:{48.,0.})openings.arr.push_back(V::makeStr(planarFaceAt(body,solid,end)));
    V request=V::makeObj();request.set("faces",openings);request.set("op",V::makeStr("shell"));request.set("input_brep",*body.find("brep_base64"));request.set("input_topology",*body.find("topology"));request.set("thickness",V::makeNum(-1.6));
    const auto shell=modelSolidNative(request);const auto shape=readBrep(shell.find("brep_base64")->str);const auto& report=*shell.find("report");
    size_t offsets=0;for(const auto& face:shapes(shape,TopAbs_FACE))offsets+=BRepAdaptor_Surface(TopoDS::Face(face)).GetType()==GeomAbs_OffsetSurface;
    check(offsets>0,"shell keeps exact offset supports");
    // No independent reference here: the fixture line compares with the piecewise
    // area. On unmodified code the shell above is rejected where OCCT misses.
    const double piecewise=piecewiseArea(shape);const auto native=verify("shelled loft",shape,piecewise,0,Verdict::Either);
    relative(report.find("area_mm2")->num,native.passes?native.area:piecewise,1e-11,"shelled loft: report publishes the dispatched area");
    // A saved copy of the same result restores instead of being rejected.
    V resolved=V::makeObj();resolved.set("operation",V::makeStr("model_solid"));resolved.set("request",request);
    auto saved=completeSelectionReferences(shell);const auto restored=restoreNativeResult(resolved,saved);
    check(restored.find("brep_base64")->str==shell.find("brep_base64")->str,"saved shell restores its exact geometry");relative(restored.find("report")->find("area_mm2")->num,report.find("area_mm2")->num,1e-11,"restored shell area");
}
void unresolvedAreaIsRejected(){
    const auto start=std::chrono::steady_clock::now();
    // Rational derivatives far from the origin carry rounding noise three orders
    // above the precision: no partition converges and the work limit ends it.
    const auto rational=sweptProfile(4e11);const double weights[]={1,.9,1.2,.8,1.1,1};for(int i=1;i<=2;++i)for(int j=1;j<=6;++j)rational->SetWeight(i,j,weights[j-1]*(i==1?1:1.05));
    const auto noisy=boxed(new Geom_OffsetSurface(rational,1.5));const auto native=occt(noisy);
    const auto direct=rejection([&]{piecewiseArea(noisy);}),published=rejection([&]{area(noisy);}),mixed=rejection([&]{area(compound({BRepPrimAPI_MakeBox(1,1,1).Shape(),noisy}));});
    std::cout<<"FIXTURE unresolvable offset: occt="<<std::setprecision(17)<<native.area<<std::setprecision(6)<<" estimate/gate="<<native.margin()<<" verdict="<<(native.passes?"pass":"miss")<<" piecewise=\""<<direct<<"\" properties=\""<<published<<"\" with-box=\""<<mixed<<"\""<<std::endl;
    check(!native.passes&&native.margin()>=kRobust,"unresolvable offset: the estimate must miss by a wide margin");
    check(direct==kRejection,"unresolvable area is rejected explicitly");check(published==kRejection,"properties does not fall back to the imprecise OCCT area");check(mixed==kRejection,"a resolved part cannot hide an unresolved face");
    // A face without a p-curve has no integrable domain.
    BRep_Builder builder;TopoDS_Face open;Handle(Geom_Surface) offset=new Geom_OffsetSurface(sweptProfile(),1.5);builder.MakeFace(open,offset,1e-7);TopoDS_Wire wire;builder.MakeWire(wire);builder.Add(wire,BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(1,1,1)).Edge());builder.Add(open,wire);
    const auto uncurved=rejection([&]{piecewiseArea(open);});check(uncurved==kRejection,"a boundary without a p-curve is rejected explicitly: \""+uncurved+"\"");
    const auto unbounded=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1))).Face();const auto infinite=rejection([&]{piecewiseArea(unbounded);});check(infinite==kRejection,"an unbounded domain is rejected explicitly: \""+infinite+"\"");
    // Informative only; the evaluation limits and the ctest timeout bound it.
    std::cout<<"rejections took "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s"<<std::endl;
}
// Every group runs, also after a failed one.
void group(const char* name,void(*body)()){
    const int before=failures;
    try{body();}catch(const std::exception& error){++failures;std::cout<<"FAILED: group "<<name<<" stopped: "<<error.what()<<std::endl;}catch(...){++failures;std::cout<<"FAILED: group "<<name<<" stopped by an unknown exception"<<std::endl;}
    std::cout<<"GROUP "<<name<<": "<<(failures==before?"passed":"FAILED")<<std::endl;
}
}
int main(){
    group("passing shapes keep their exact area",passingShapesKeepTheirExactArea);
    group("knot jumps of an offset",knotJumpsOfAnOffset);
    group("trimmed doubly curved offset",trimmedOffset);
    group("every face of a missed shape is integrated again",everyFaceOfAMissedShapeIsIntegratedAgain);
    group("shelled loft",shelledLoft);
    group("unresolved area is rejected",unresolvedAreaIsRejected);
    std::cout<<checks<<" exact area integration checks, "<<failures<<" failed"<<std::endl;return failures?1:0;
}
