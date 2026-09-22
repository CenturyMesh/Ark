#include "ark/notch.hpp"
#include "ark/core.hpp"
#include <stdexcept>
namespace ark {
namespace {
using V=arkjson::Value;
double number(const V& j,const char* key){auto p=j.find(key);if(!p||p->t!=V::Num||!std::isfinite(p->num))throw std::runtime_error(std::string("Expected finite number: ")+key);return p->num;}
V vec(Vec3 p){V a=V::makeArr();for(auto x:{p.x,p.y,p.z})a.arr.push_back(V::makeNum(x));return a;}
}
arkjson::Value planNotchedBlank(const V& request){
    double width=number(request,"width_mm"),height=number(request,"height_mm"),wall=number(request,"wall_mm"),length=number(request,"length_mm"),hinge=number(request,"hinge_offset_mm");
    if(width<=0||height<=0||width>10000||height>10000||wall<=0||2*wall>=std::min(width,height)||length<=0||length>1e6||hinge<0||hinge>wall)
        throw std::runtime_error("Invalid section/length or hinge offset outside retained wall");
    auto list=request.find("notches");if(!list||list->t!=V::Arr||list->arr.empty()||list->arr.size()>64)throw std::runtime_error("notches must contain 1..64 entries");
    struct Notch {double station,angle,depth,breadth,lo,hi;std::string face;Mat4 frame;};
    std::vector<Notch> specs;
    for(auto& j:list->arr){
        double station=number(j,"station_mm"),angle=number(j,"angle_deg");auto face=j.find("hinge_face");
        if(!face||face->t!=V::Str||angle<=0||angle>=170)throw std::runtime_error("Specify hinge_face and angle in (0,170) degrees");
        Notch n{station,angle,height,width,0,0,face->str,Mat4{}};
        if(n.face=="top")n.frame=Mat4::translation({0,height,width})*Mat4::rotX(K_PI);
        else if(n.face=="left"){n.depth=width;n.breadth=height;n.frame=Mat4::translation({0,height,0})*Mat4::rotX(K_HALF_PI);}
        else if(n.face=="right"){n.depth=width;n.breadth=height;n.frame=Mat4::translation({0,0,width})*Mat4::rotX(-K_HALF_PI);}
        else if(n.face!="bottom")throw std::runtime_error("hinge_face must be bottom, top, left or right");
        double half=(n.depth-hinge)*std::tan(deg2rad(angle)*.5);n.lo=station-half;n.hi=station+half;
        if(n.lo<=0||n.hi>=length)throw std::runtime_error("Notch reaches a stock end");
        specs.push_back(n);
    }
    std::sort(specs.begin(),specs.end(),[](const Notch&a,const Notch&b){return a.lo<b.lo;});
    for(size_t i=1;i<specs.size();++i)if(specs[i].lo<=specs[i-1].hi)throw std::runtime_error("Overlapping notch envelopes are not supported");
    V result=V::makeObj(),stock=V::makeObj(),notches=V::makeArr(),proof=V::makeObj();
    stock.set("width_mm",V::makeNum(width));stock.set("height_mm",V::makeNum(height));stock.set("wall_mm",V::makeNum(wall));stock.set("length_mm",V::makeNum(length));stock.set("hinge_offset_mm",V::makeNum(hinge));
    double removed=0,maxClosure=0,maxRoundtrip=0;
    for(auto& n:specs){
        double alpha=deg2rad(n.angle)/2,q=std::tan(alpha);
        double a=wall-hinge,b=n.depth-wall-hinge,c=n.depth-hinge;
        double volume=q*(2*wall*(b*b-a*a)+n.breadth*(c*c-b*b));removed+=volume;
        V j=V::makeObj(),polygon=V::makeArr(),matrix=V::makeArr();
        j.set("station_mm",V::makeNum(n.station));j.set("angle_deg",V::makeNum(n.angle));j.set("hinge_face",V::makeStr(n.face));
        j.set("opening_mm",V::makeNum(2*c*q));j.set("removed_volume_mm3",V::makeNum(volume));
        const double margin=1.;
        for(Vec3 p:std::vector<Vec3>{{n.station-a*q,wall,-margin},{n.station-(c+margin)*q,n.depth+margin,-margin},{n.station+(c+margin)*q,n.depth+margin,-margin},{n.station+a*q,wall,-margin}})polygon.arr.push_back(vec(n.frame.transformPoint(p)));
        j.set("cutter_polygon",polygon);j.set("cutter_extrusion",vec(n.frame.transformDir({0,0,n.breadth+2*margin})));
        for(double x:n.frame.m)matrix.arr.push_back(V::makeNum(x));
        j.set("frame_column_major",matrix);
        j.set("hinge_axis_point",vec(n.frame.transformPoint({n.station,hinge,0})));j.set("hinge_axis_direction",vec(n.frame.transformDir({0,0,1})));
        // Parametric cut-plane identity. Sampling checks numerical implementation,
        // not physical ligament strain: R(-a)*(-d*tan(a),d)=R(a)*(d*tan(a),d).
        for(int k=0;k<=16;++k){double d=lerp(a,c,k/16.);
            Vec3 left{-d*q,d,0},right{d*q,d,0};auto l=Mat4::rotZ(-alpha).transformDir(left),r=Mat4::rotZ(alpha).transformDir(right);
            maxClosure=std::max(maxClosure,(l-r).len());maxRoundtrip=std::max(maxRoundtrip,(Mat4::rotZ(alpha).transformDir(l)-left).len());
        }
        notches.arr.push_back(j);
    }
    double stockVolume=(width*height-(width-2*wall)*(height-2*wall))*length;
    result.set("format",V::makeStr("ark-cut-plan"));result.set("version",V::makeNum(1));result.set("stock",stock);result.set("notches",notches);
    result.set("model",V::makeStr("ideal_zero_radius_hinge_preview"));result.set("manufacturing_authoritative",V::makeBool(false));result.set("source_reconstruction_verified",V::makeBool(false));
    proof.set("cut_face_closure_max_mm",V::makeNum(maxClosure));proof.set("cut_face_inverse_max_mm",V::makeNum(maxRoundtrip));
    proof.set("stock_volume_mm3",V::makeNum(stockVolume));proof.set("removed_volume_mm3",V::makeNum(removed));proof.set("remaining_volume_mm3",V::makeNum(stockVolume-removed));
    proof.set("finite_wall_deformation_verified",V::makeBool(false));proof.set("kerf_compensated",V::makeBool(false));proof.set("springback_compensated",V::makeBool(false));result.set("validation",proof);
    return result;
}
}
